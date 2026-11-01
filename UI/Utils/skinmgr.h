#ifndef SKINMGR_H
#define SKINMGR_H

#include <QObject>

typedef enum SkinId
{
    SK_Dialog = 0,
    SK_Login,
    SK_WorkSpace,
    SK_Screen,
    SK_SceneEditor,  // [新增] 场景编辑器样式
    SK_Profile,
    SK_Universal,
    SK_SceneCreate
} SkinID;

typedef struct QssFile
{
    SkinId id;
    char const* name;
} QssFile;

class SkinMgr : public QObject
{
    Q_OBJECT
public:
    static SkinMgr* inst()
    {
        static SkinMgr* _inst = 0;
        if (!_inst)
        {
            _inst = new SkinMgr(0);
        }
        return _inst;
    }

    void setSkin(SkinId id, QWidget* w);

private:
    explicit SkinMgr(QObject* parent = 0);
    QString loadQss(QString qssFileName);  // 参数改为文件名
    QString dealLine(QString const& lineText, qreal dip);
};

#endif  // SKINMGR_H
