#include "skinmgr.h"
#include <QWidget>
#include <QFile>
#include <QApplication>
#include <QScreen>
#include <QRegularExpression>
#include <QDebug>
#include <QCoreApplication>
#include <QDir>

// 获取屏幕 DPI 缩放比例
static double scale_dip()
{
    static double dip = QApplication::primaryScreen()->logicalDotsPerInch() / 96.0f;
    return dip < 1.0 ? 1.0 : dip;
}

// [修改] 定义相对路径的文件名
QssFile g_qss[] = {{SK_Dialog, ":/UI/QSS/skin/dialog.css"},  // 假设这是主测试
                   {SK_Login, ":/UI/QSS/skin/login.css"},
                   {SK_WorkSpace, ":/UI/QSS/skin/workspace.css"},
                   {SK_Screen, ":/UI/QSS/skin/screen.css"},
                   {SK_SceneEditor, ":/UI/QSS/skin/scene_editor.css"},  // [新增]
                   {SK_Profile, ":/UI/QSS/skin/profile.css"},
                   {SK_Universal, ":/UI/QSS/skin/Universal.css"},  // [新增]
                   {SK_SceneCreate, ":/UI/QSS/skin/scene_create.css"}};

SkinMgr::SkinMgr(QObject* parent) : QObject(parent)
{
}

void SkinMgr::setSkin(SkinId id, QWidget* w)
{
    QString fileName = "";
    for (auto const& item : g_qss)
    {
        if (item.id == id)
        {
            fileName = item.name;
            break;
        }
    }

    if (fileName.isEmpty())
        return;

    QString style = loadQss(fileName);
    if (!style.isEmpty())
    {
        w->setStyleSheet(style);
    }
}

QString SkinMgr::loadQss(QString qssFileName)
{
    QFile file(qssFileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qDebug() << "Error: Cannot open QSS file:" << qssFileName;
        return "";
    }

    QString resultQss = "";
    double dip = scale_dip();

    if (dip == 1.0)
    {
        resultQss = QString::fromUtf8(file.readAll());
    }
    else
    {
        while (!file.atEnd())
        {
            QByteArray lineData = file.readLine();
            QString lineStr = QString::fromUtf8(lineData);
            resultQss += dealLine(lineStr, dip);
        }
    }
    file.close();
    return resultQss;
}

QString SkinMgr::dealLine(QString const& lineText, qreal dip)
{
    QString line = lineText;
    // 匹配如 "100px", "12px" 等格式
    QRegularExpression reg("(\\d{1,4})px");
    QRegularExpressionMatch match;

    int start = 0;
    while ((match = reg.match(line, start)).hasMatch())
    {
        // 获取数字部分
        QString numStr = match.captured(1);

        // 计算缩放后的数值
        int calcNum = static_cast<int>(numStr.toInt() * dip);
        QString replaceNum = QString::number(calcNum);

        // 替换原数字 (保留 px 单位在外部，这里实际上是把数字替换了)
        // 注意：正则匹配的是 "100px"，但 captured(1) 是 "100"
        // 我们要替换的位置是数字的起始位置
        int matchPos = match.capturedStart(1);
        int matchLen = match.capturedLength(1);

        line.replace(matchPos, matchLen, replaceNum);

        // 更新搜索起始位置，避免死循环或重复替换
        // 这里的逻辑稍微复杂，因为替换后长度可能变化
        // 新的开始位置 = 匹配到的位置 + 新数字的长度 + "px".length() (2)
        // 实际上 match.capturedEnd(0) 会包含px，我们需要根据新字符串调整
        // 简单做法：从 matchPos + replaceNum.length() 开始即可
        start = matchPos + replaceNum.length();
    }
    return line;
}
