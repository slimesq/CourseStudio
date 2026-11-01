#pragma once
#include <QString>
#include <QMap>

class ApiConfig
{
public:
    static ApiConfig& instance()
    {
        static ApiConfig s;
        return s;
    }

    // 获取完整 URL
    QString getUrl(QString const& key)
    {
        if (!m_urls.contains(key))
            return m_baseUrl + "/404";
        return m_baseUrl + m_urls[key];
    }

    // 获取基础 BaseURL
    QString baseUrl() const
    {
        return m_baseUrl;
    }
    void setBaseUrl(QString const& url)
    {
        m_baseUrl = url;
    }

    void loadConfig()
    {
        m_baseUrl = "http://192.168.6.100:8085";

        // --- 认证相关 ---
        m_urls["AUTH_SIMPLE_LOGIN"] = "/auth/login";  // 扫码登录接口

        // --- 用户相关 ---
        m_urls["USER_PROFILE"] = "/system/user/profile";  // 基础信息(头像等)
        m_urls["USER_INFO"] = "/system/user/getInfo";     // [New] 详细信息(含权限)
        m_urls["USER_LIST"] = "/system/user/list";

        // --- 场景相关 ---
        m_urls["SCENE_ADD"] = "/resource/conf/layout";
        m_urls["SCENE_UPDATE"] = "/resource/conf/layout";
        m_urls["SCENE_DELETE"] = "/resource/conf/layout/";
        m_urls["SCENE_DETAIL"] = "/resource/conf/layout/";
        m_urls["SCENE_LIST"] = "/resource/conf/layout/list";
        m_urls["SCENE_EXPORT"] = "/resource/conf/layout/export";
        m_urls["SCENE_UPLOAD"] = "/resource/conf/layout";

        // --- 场景分类相关 ---
        m_urls["SCENE_CLASS_ADD"] = "/resource/conf/layoutType";
        m_urls["SCENE_CLASS_UPDATE"] = "/resource/conf/layoutType";
        m_urls["SCENE_CLASS_DELETE"] = "/resource/conf/layoutType/";
        m_urls["SCENE_CLASS_DETAIL"] = "/resource/conf/layoutType/";
        m_urls["SCENE_CLASS_LIST"] = "/resource/conf/layoutType/list";
        m_urls["SCENE_CLASS_EXPORT"] = "/resource/conf/layoutType/export";

        // --- 素材相关 ---
        m_urls["ASSET_UPLOAD"] = "/resource/media/layout/upload";
        m_urls["ASSET_UPDATE"] = "/resource/conf/media";
        m_urls["ASSET_DELETE"] = "/resource/conf/media/";
        m_urls["ASSET_DETAIL"] = "/resource/conf/media/";
        m_urls["ASSET_LIST"] = "/resource/conf/media/list";
        m_urls["ASSET_EXPORT"] = "/resource/conf/media/export";

        // --- 素材分组相关 ---
        m_urls["ASSET_GROUP_ADD"] = "/resource/conf/mediaGroup";
        m_urls["ASSET_GROUP_UPDATE"] = "/resource/conf/mediaGroup";
        m_urls["ASSET_GROUP_DELETE"] = "/resource/conf/mediaGroup/";
        m_urls["ASSET_GROUP_DETAIL"] = "/resource/conf/mediaGroup/";
        m_urls["ASSET_GROUP_LIST"] = "/resource/conf/mediaGroup/list";
        m_urls["ASSET_GROUP_EXPORT"] = "/resource/conf/mediaGroup/export";

        // --- [New] 场景封面相关 ---
        // 引用来源: 场景封面上传.md
        m_urls["LAYOUT_COVER_UPLOAD"] = "/resource/media/layoutCover/upload";

        // --- [New] 直播间相关 ---
        // 引用来源: 直播间.md
        m_urls["LIVE_ROOM_LIST"] = "/resource/live/room/mylist";
        m_urls["LIVE_ROOM_DETAIL"] = "/resource/live/room/";  // 使用时需拼接 {roomId}
        m_urls["LIVE_ROOM_STATUS"] = "/resource/live/room/statusChange";

        // --- [New] 场景个人偏好相关 ---
        // 引用来源: 个人偏好设置.md
        m_urls["PREF_GET"] = "/resource/layoutPreference/getMine";
        m_urls["PREF_SET"] = "/resource/layoutPreference/setMine";

        // 1. 直播录制文件上传 (POST /resource/media/liveRecord/upload)
        m_urls["LIVE_RECORD_UPLOAD"] = "/resource/media/liveRecord/upload";

        // 2. 直播过程文件(PCM/YUV)上传 (POST /resource/media/live/upload)
        m_urls["LIVE_FILE_UPLOAD"] = "/resource/media/live/upload";

        m_urls["LIVE_ROOM_EXCEPTION_STATUS"] = "/resource/live/room/status/";
    }

private:
    ApiConfig()
    {
        loadConfig();
    }
    QString m_baseUrl;
    QMap<QString, QString> m_urls;
};
