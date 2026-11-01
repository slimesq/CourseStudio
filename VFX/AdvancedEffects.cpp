#include "AdvancedEffects.h"
#include <vector>

// =========================================================
// 8. Face Reshape Implementation
// =========================================================
void FaceReshapeEffect::setParameter(std::string const& key, QVariant const& value)
{
    if (key == "slim")
        m_slimLevel = value.toInt();
    if (key == "eye")
        m_eyeLevel = value.toInt();
}

void FaceReshapeEffect::warpLocal(cv::Mat& img, cv::Point2f const& center, float r, float strength)
{
    // [Optimization] If strength is too small, skip expensive remap
    if (std::abs(strength) < 0.001f)
        return;

    int radius = (int)r;
    // Basic bounds check
    if (center.x < -radius || center.y < -radius || center.x > img.cols + radius ||
        center.y > img.rows + radius)
        return;

    int x1 = std::max(0, (int)(center.x - radius));
    int y1 = std::max(0, (int)(center.y - radius));
    int x2 = std::min(img.cols, (int)(center.x + radius));
    int y2 = std::min(img.rows, (int)(center.y + radius));

    cv::Rect roiRect(x1, y1, x2 - x1, y2 - y1);
    if (roiRect.width <= 0 || roiRect.height <= 0)
        return;

    cv::Mat mapX(roiRect.size(), CV_32FC1);
    cv::Mat mapY(roiRect.size(), CV_32FC1);

    float r2 = r * r;

    for (int y = 0; y < mapX.rows; y++)
    {
        float* ptrX = mapX.ptr<float>(y);
        float* ptrY = mapY.ptr<float>(y);

        float absY = (float)(y + y1);
        float dy = absY - center.y;
        float dy2 = dy * dy;

        for (int x = 0; x < mapX.cols; x++)
        {
            float absX = (float)(x + x1);
            float dx = absX - center.x;
            float dist2 = dx * dx + dy2;  // Optimization: move dy2 out

            if (dist2 < r2)
            {
                float dist = std::sqrt(dist2);
                float ratio = dist / r;
                float alpha = 1.0f - ratio;
                float factor = alpha * alpha * strength;

                // Relative coordinate for remap (0..width, 0..height of ROI)
                // Source pos = Current Abs Pos - Displacement
                // But map values must be absolute coordinates if we were mapping the whole image.
                // Since we are mapping a ROI, remap expects coordinates RELATIVE TO THE ROI image source?
                // NO! cv::remap(src, dst, mapX, mapY)
                // If mapX(0,0) = 10, it samples src(10, ...).
                // Our 'src' is img(roiRect). Its (0,0) is actually (x1, y1) of global.
                // So if we want to sample (x1, y1), mapX should be 0.

                // Target: We want to find which pixel from 'src' goes to (x,y) in 'dst'.
                // Formula: SamplePos = CurrentPos - Vector * Factor

                float sampleAbsX = absX - dx * factor;
                float sampleAbsY = absY - dy * factor;

                // Convert absolute sample pos to relative ROI pos
                ptrX[x] = sampleAbsX - x1;
                ptrY[x] = sampleAbsY - y1;
            }
            else
            {
                // Identity map (relative to ROI)
                ptrX[x] = (float)x;
                ptrY[x] = (float)y;
            }
        }
    }

    cv::Mat roiSrc = img(roiRect);
    cv::Mat roiDst;
    cv::remap(roiSrc, roiDst, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
    roiDst.copyTo(img(roiRect));
}

void FaceReshapeEffect::process(cv::Mat& img)
{
    if (!m_face.hasFace || m_face.landmarks.size() < 5)
        return;

    // --- Big Eye (大眼) ---
    // Increase threshold to avoid jitter when slider is low
    if (m_eyeLevel > 0)
    {
        cv::Point2f rightEye = m_face.landmarks[0];
        cv::Point2f leftEye = m_face.landmarks[1];

        float eyeDist = cv::norm(rightEye - leftEye);
        float radius = eyeDist * 0.55f;  // Slightly larger coverage

        // Map 0-100 to 0.0 - 0.4 (Negative for expansion)
        // [Logic Check] To expand, we need to pull pixels from INSIDE towards OUTSIDE?
        // No, to expand an eye, we push pixels AWAY from center.
        // So at R, we sample from R-delta (closer to center).
        // My warpLocal formula: sample = current - vec * factor.
        // If factor < 0 (negative), sample = current + vec * abs(factor).
        // Sample is FURTHER OUT. This shrinks the content (pulls background into eye).
        // If factor > 0 (positive), sample = current - vec * factor (Closer to center).
        // This pulls center pixels out. This is EXPANSION.

        // Wait, let's verify standard warping.
        // We want the eye to look bigger.
        // So a pixel at radius R should look like the pupil (center).
        // So dst(R) = src(0).
        // current(R) -> sample(0).
        // sample = current - vec * factor.
        // 0 = R - R * factor => factor = 1. (Positive)

        // So: Positive Strength = Expansion (Big Eye).
        //     Negative Strength = Contraction (Slim Face).

        // Let's swap the signs from previous code if it was confusing!

        float strength = (m_eyeLevel / 100.0f) * 0.5f;  // POSITIVE for Big Eye

        warpLocal(img, leftEye, radius, strength);
        warpLocal(img, rightEye, radius, strength);
    }

    // --- Face Slimming (瘦脸) ---
    if (m_slimLevel > 0)
    {
        cv::Point2f nose = m_face.landmarks[2];
        cv::Point2f rightMouth = m_face.landmarks[3];
        cv::Point2f leftMouth = m_face.landmarks[4];

        cv::Point2f vLeft = leftMouth - nose;
        cv::Point2f vRight = rightMouth - nose;

        cv::Point2f leftCheek = leftMouth + vLeft * 0.5f;
        cv::Point2f rightCheek = rightMouth + vRight * 0.5f;

        float radius = cv::norm(leftMouth - rightMouth) * 1.0f;

        // Slimming = Contraction. Pushing cheek in.
        // Pixel at Center (Cheek) needs to look like pixel at Radius (Background).
        // dst(0) = src(R).
        // sample = current - vec * factor
        // R = 0 - 0 * f (Doesn't work at center).
        // Let's look at pixel at R. It should stay at R.
        // Pixel at R/2 should move to 0.
        // dst(0) = src(R/2).
        // This is Contraction.
        // Based on logic above: Negative Strength = Contraction.

        float strength = -1.0f * (m_slimLevel / 100.0f) * 0.3f;  // NEGATIVE for Slim

        warpLocal(img, leftCheek, radius, strength);
        warpLocal(img, rightCheek, radius, strength);
    }
}

// =========================================================
// 9. Makeup Implementation
// =========================================================
void MakeupEffect::setParameter(std::string const& key, QVariant const& value)
{
    if (key == "level")
        m_lipstickLevel = value.toInt();
}

void MakeupEffect::process(cv::Mat& img)
{
    if (!m_face.hasFace || m_face.landmarks.size() < 68)
    {
        // 回退方案：简单的嘴部区域红润化 (基于 5 点)
        if (m_face.landmarks.size() == 5 && m_lipstickLevel > 0)
        {
            cv::Point2f leftMouth = m_face.landmarks[4];
            cv::Point2f rightMouth = m_face.landmarks[3];
            cv::Point2f center = (leftMouth + rightMouth) * 0.5f;

            // 椭圆掩码
            float width = cv::norm(leftMouth - rightMouth) * 0.6f;
            float height = width * 0.4f;

            cv::Mat mask = cv::Mat::zeros(img.size(), CV_8UC1);
            cv::ellipse(mask, center, cv::Size(width, height), 0, 0, 360, cv::Scalar(255), -1);
            cv::GaussianBlur(mask, mask, cv::Size(15, 15), 0);  // 大羽化

            float alpha = m_lipstickLevel / 200.0f;  // 降低不透明度，因为很粗糙

            // 快速混合
            int x1 = std::max(0, (int)(center.x - width - 10));
            int y1 = std::max(0, (int)(center.y - height - 10));
            int x2 = std::min(img.cols, (int)(center.x + width + 10));
            int y2 = std::min(img.rows, (int)(center.y + height + 10));

            for (int y = y1; y < y2; y++)
            {
                uchar* mPtr = mask.ptr<uchar>(y);
                cv::Vec3b* iPtr = img.ptr<cv::Vec3b>(y);
                for (int x = x1; x < x2; x++)
                {
                    if (mPtr[x] > 0)
                    {
                        float k = (mPtr[x] / 255.0f) * alpha;
                        // 叠加红色 (BGR: 0, 0, 255)
                        iPtr[x][1] = iPtr[x][1] * (1 - k * 0.5);  // G 减淡
                        iPtr[x][2] =
                            std::min(255.0f, iPtr[x][2] + 50.0f * k + iPtr[x][2] * k);  // R 增强
                    }
                }
            }
        }
        return;
    }
}
