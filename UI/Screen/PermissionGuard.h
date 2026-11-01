#ifndef PERMISSIONGUARD_H
#define PERMISSIONGUARD_H

#include <QString>
#include <QObject>
#include "UserMgr.h"
#include "UniversalDialog.h"

class PermissionGuard
{
public:
    // --- Permission Constants ---
    // Layout Type (Category)
    static constexpr char const* PERM_LAYOUT_TYPE_QUERY = "conf:layoutType:query";
    static constexpr char const* PERM_LAYOUT_TYPE_ADD = "conf:layoutType:add";
    static constexpr char const* PERM_LAYOUT_TYPE_EDIT = "conf:layoutType:edit";
    static constexpr char const* PERM_LAYOUT_TYPE_DEL = "conf:layoutType:delete";

    // Layout (Scene)
    static constexpr char const* PERM_LAYOUT_QUERY = "conf:layout:query";
    static constexpr char const* PERM_LAYOUT_ADD = "conf:layout:add";
    static constexpr char const* PERM_LAYOUT_EDIT = "conf:layout:edit";
    static constexpr char const* PERM_LAYOUT_DEL = "conf:layout:delete";

    // Media Group
    static constexpr char const* PERM_MEDIA_GROUP_QUERY = "conf:mediaGroup:query";
    static constexpr char const* PERM_MEDIA_GROUP_ADD = "conf:mediaGroup:add";
    static constexpr char const* PERM_MEDIA_GROUP_EDIT = "conf:mediaGroup:edit";
    static constexpr char const* PERM_MEDIA_GROUP_DEL = "conf:mediaGroup:delete";

    // Media (Asset)
    static constexpr char const* PERM_MEDIA_QUERY = "conf:media:query";
    static constexpr char const* PERM_MEDIA_ADD = "conf:media:add";
    static constexpr char const* PERM_MEDIA_EDIT = "conf:media:edit";
    static constexpr char const* PERM_MEDIA_DEL = "conf:media:delete";

    // --- Core Check Functions ---

    // Check strictly against UserMgr
    static bool check(QString const& perm)
    {
        return UserMgr::inst()->hasPermission(perm);
    }

    // Check and show dialog if failed
    // Returns true if allowed, false if denied (and dialog shown)
    static bool checkAndShow(QWidget* parent, QString const& perm, QString const& actionName)
    {
        if (check(perm))
            return true;

        QString msg = QString("您没有权限执行“%1”操作。\n(缺少权限: %2)").arg(actionName).arg(perm);
        UniversalDialog::showError(parent, "权限不足", msg);
        return false;
    }

    // --- Specific Business Checks ---

    // Can user see the scene list?
    static bool canQueryScenes()
    {
        return check(PERM_LAYOUT_QUERY);
    }

    // Can user create a new scene? (Requires Layout Add)
    // Note: Creating a *new category* implies LayoutType Add, checked separately inside creation widget if needed
    static bool canAddScene(QWidget* parent)
    {
        return checkAndShow(parent, PERM_LAYOUT_ADD, "新建场景");
    }

    // Can user edit scene name?
    static bool canEditScene(QWidget* parent)
    {
        return checkAndShow(parent, PERM_LAYOUT_EDIT, "编辑场景");
    }

    // Can user delete scene?
    static bool canDeleteScene(QWidget* parent)
    {
        return checkAndShow(parent, PERM_LAYOUT_DEL, "删除场景");
    }

    // Can user add media/asset?
    static bool canAddMedia(QWidget* parent)
    {
        return checkAndShow(parent, PERM_MEDIA_ADD, "添加素材");
    }

    // Can user create new category?
    static bool canAddCategory(QWidget* parent)
    {
        return checkAndShow(parent, PERM_LAYOUT_TYPE_ADD, "新建分类");
    }
};

#endif  // PERMISSIONGUARD_H
