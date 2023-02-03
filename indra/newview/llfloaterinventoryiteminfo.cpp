/** 
 * @file llfloaterinventoryiteminfo.cpp
 * @brief Information about inventory items
 *
 * $LicenseInfo:firstyear=2023&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2023, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llfloaterinventoryiteminfo.h"

#include "llagent.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llexperiencecache.h"
#include "llinventorydefines.h"
#include "llinventorymodel.h"
#include "lllineeditor.h"
#include "roles_constants.h"
#include "lltrans.h"
#include "llviewerinventory.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"


class PropertiesChangedCallback : public LLInventoryCallback
{
public:
    PropertiesChangedCallback(LLHandle<LLFloater> sidepanel_handle, LLUUID &item_id, S32 id)
        : mHandle(sidepanel_handle), mItemId(item_id), mId(id)
    {}

    void fire(const LLUUID &inv_item)
    {
        // inv_item can be null for some reason
        LLFloaterInventoryItemProperties* floater = dynamic_cast<LLFloaterInventoryItemProperties*>(mHandle.get());
        if (floater)
        {
            // sidepanel waits only for most recent update
            floater->onUpdateCallback(mItemId, mId);
        }
    }
private:
    LLHandle<LLFloater> mHandle;
    LLUUID mItemId;
    S32 mId;
};

//-----------------------------------------------------------------------------
// LLFloaterItemProperties
//-----------------------------------------------------------------------------

LLFloaterInventoryItemProperties::LLFloaterInventoryItemProperties(const LLSD& key)
: LLFloater(key)
, mObjectInventoryObserver(NULL)
, mUpdatePendingId(-1)
{
    if (key.isUUID())
    {
        setItemID(key.asUUID());
    }
    else if (key.isMap())
    {
        setItemID(key["item_id"].asUUID());
        setObjectID(key["task_id"].asUUID());
    }
    else
    {
        // TODO: make sure won't crash if opening without params
        LL_WARNS() << "Opening LLFloaterInventoryItemProperties without proper params" << LL_ENDL;
    }

    gInventory.addObserver(this);
}

LLFloaterInventoryItemProperties::~LLFloaterInventoryItemProperties()
{
    gInventory.removeObserver(this);

    stopObjectInventoryObserver();
}

BOOL LLFloaterInventoryItemProperties::postBuild()
{
    mItemNameCtrl = getChild<LLLineEditor>("LabelItemName");
    mItemDescriptionCtrl = getChild<LLLineEditor>("LabelItemDesc");
    mItemExperienceNameCtrl = getChild<LLTextBox>("LabelItemExperience");
    mItemExperienceTitleCtrl = getChild<LLUICtrl>("LabelItemExperienceTitle");
    mAcquiredDateCtrl = getChild<LLUICtrl>("LabelAcquiredDate");
    mIconLockedCtrl = getChild<LLUICtrl>("IconLocked");

    mOwnerModifyCtrl = getChild<LLCheckBoxCtrl>("CheckOwnerModify");
    mOwnerCopyCtrl = getChild<LLCheckBoxCtrl>("CheckOwnerCopy");
    mOwnerTransferCtrl = getChild<LLCheckBoxCtrl>("CheckOwnerTransfer");
    mShareWithGroupCtrl = getChild<LLCheckBoxCtrl>("CheckShareWithGroup");
    mEveryoneCopyCtrl = getChild<LLCheckBoxCtrl>("CheckEveryoneCopy");
    mNextOwnerModifyCtrl = getChild<LLCheckBoxCtrl>("CheckNextOwnerModify");
    mNextOwnerCopyCtrl = getChild<LLCheckBoxCtrl>("CheckNextOwnerCopy");
    mNextOwnerTransferCtrl = getChild<LLCheckBoxCtrl>("CheckNextOwnerTransfer");

    mCheckPurchaseCtrl = getChild<LLCheckBoxCtrl>("CheckPurchase");
    mSaleTypeCtrl = getChild<LLComboBox>("ComboBoxSaleType");
    mCostCtrl = getChild<LLUICtrl>("Edit Cost");

    mItemNameCtrl->setPrevalidate(&LLTextValidate::validateASCIIPrintableNoPipe);
    mItemNameCtrl->setCommitCallback(boost::bind(&LLFloaterInventoryItemProperties::onCommitName, this));
    mItemDescriptionCtrl->setPrevalidate(&LLTextValidate::validateASCIIPrintableNoPipe);
    mItemDescriptionCtrl->setCommitCallback(boost::bind(&LLFloaterInventoryItemProperties::onCommitDescription, this));

    mShareWithGroupCtrl->setCommitCallback(boost::bind(&LLFloaterInventoryItemProperties::onCommitPermissions, this, _1));
    mEveryoneCopyCtrl->setCommitCallback(boost::bind(&LLFloaterInventoryItemProperties::onCommitPermissions, this, _1));
    mNextOwnerModifyCtrl->setCommitCallback(boost::bind(&LLFloaterInventoryItemProperties::onCommitPermissions, this, _1));
    mNextOwnerCopyCtrl->setCommitCallback(boost::bind(&LLFloaterInventoryItemProperties::onCommitPermissions, this, _1));
    mNextOwnerTransferCtrl->setCommitCallback(boost::bind(&LLFloaterInventoryItemProperties::onCommitPermissions, this, _1));

    mCheckPurchaseCtrl->setCommitCallback(boost::bind(&LLFloaterInventoryItemProperties::onCommitSaleInfo, this, _1));
    mSaleTypeCtrl->setCommitCallback(boost::bind(&LLFloaterInventoryItemProperties::onCommitSaleInfo, this, _1));
    mCostCtrl->setCommitCallback(boost::bind(&LLFloaterInventoryItemProperties::onCommitSaleInfo, this, _1));

    refresh();

    return LLFloater::postBuild();
}

void LLFloaterInventoryItemProperties::onOpen(const LLSD& key)
{
    // Tell the panel which item it needs to visualize
    /*LLSidepanelItemInfo* panel = getChild<LLSidepanelItemInfo>("item_panel");
    panel->setItemID(key["id"].asUUID());*/
}

void LLFloaterInventoryItemProperties::setObjectID(const LLUUID& object_id)
{
    mObjectID = object_id;

    // Start monitoring changes in the object inventory to update
    // selected inventory item properties in Item Profile panel. See STORM-148.
    startObjectInventoryObserver();
    mUpdatePendingId = -1;
}

void LLFloaterInventoryItemProperties::setItemID(const LLUUID& item_id)
{
    if (mItemID != item_id)
    {
        mItemID = item_id;
        mUpdatePendingId = -1;
    }
}

const LLUUID& LLFloaterInventoryItemProperties::getObjectID() const
{
    return mObjectID;
}

const LLUUID& LLFloaterInventoryItemProperties::getItemID() const
{
    return mItemID;
}

void LLFloaterInventoryItemProperties::onUpdateCallback(const LLUUID& item_id, S32 received_update_id)
{
    if (mItemID == item_id && mUpdatePendingId == received_update_id)
    {
        mUpdatePendingId = -1;
        refresh();
    }
}

void LLFloaterInventoryItemProperties::refresh()
{
    LLViewerInventoryItem* item = findItem();
    if (item)
    {
        refreshFromItem(item);
    }
}

void LLFloaterInventoryItemProperties::refreshFromItem(LLViewerInventoryItem* item)
{
    ////////////////////////
    // PERMISSIONS LOOKUP //
    ////////////////////////

    llassert(item);
    if (!item) return;

    if (mUpdatePendingId != -1)
    {
        return;
    }

    // do not enable the UI for incomplete items.
    BOOL is_complete = item->isFinished();
    const BOOL cannot_restrict_permissions = LLInventoryType::cannotRestrictPermissions(item->getInventoryType());
    const BOOL is_calling_card = (item->getInventoryType() == LLInventoryType::IT_CALLINGCARD);
    const BOOL is_settings = (item->getInventoryType() == LLInventoryType::IT_SETTINGS);
    const LLPermissions& perm = item->getPermissions();
    const BOOL can_agent_manipulate = gAgent.allowOperation(PERM_OWNER, perm,
        GP_OBJECT_MANIPULATE);
    const BOOL can_agent_sell = gAgent.allowOperation(PERM_OWNER, perm,
        GP_OBJECT_SET_SALE) &&
        !cannot_restrict_permissions;
    const BOOL is_link = item->getIsLinkType();

    const LLUUID trash_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    bool not_in_trash = (item->getUUID() != trash_id) && !gInventory.isObjectDescendentOf(item->getUUID(), trash_id);

    // You need permission to modify the object
    // to modify an inventory item in it.
    LLViewerObject* object = NULL;
    if (!mObjectID.isNull()) object = gObjectList.findObject(mObjectID);
    BOOL is_obj_modify = TRUE;
    if (object)
    {
        is_obj_modify = object->permOwnerModify();
    }

    if (item->getInventoryType() == LLInventoryType::IT_LSL)
    {
        // todo: hide layout?
        mItemExperienceTitleCtrl->setVisible(TRUE);
        mItemExperienceNameCtrl->setText(getString("loading_experience"));
        mItemExperienceNameCtrl->setVisible(TRUE);
        std::string url = std::string();
        if (object && object->getRegion())
        {
            url = object->getRegion()->getCapability("GetMetadata");
        }
        LLExperienceCache::instance().fetchAssociatedExperience(item->getParentUUID(), item->getUUID(), url,
            boost::bind(&LLSidepanelItemInfo::setAssociatedExperience, getDerivedHandle<LLSidepanelItemInfo>(), _1));
    }

    //////////////////////
    // ITEM NAME & DESC //
    //////////////////////
    BOOL is_modifiable = gAgent.allowOperation(PERM_MODIFY, perm,
        GP_OBJECT_MANIPULATE)
        && is_obj_modify && is_complete && not_in_trash; // todo: why not in trash? Check if renamable

    mItemNameCtrl->setEnabled(is_modifiable && !is_calling_card); // for now, don't allow rename of calling cards
    mItemNameCtrl->setValue(item->getName());
    mItemDescriptionCtrl->setEnabled(is_modifiable);
    mIconLockedCtrl->setVisible(!is_modifiable);
    mItemDescriptionCtrl->setValue(item->getDescription());

    //////////////////
    // CREATOR NAME //
    //////////////////
    if (!gCacheName) return;
    if (!gAgent.getRegion()) return;

    if (item->getCreatorUUID().notNull())
    {
        LLUUID creator_id = item->getCreatorUUID();
        std::string name =
            LLSLURL("agent", creator_id, "completename").getSLURLString();
        getChildView("BtnCreator")->setEnabled(TRUE);
        getChildView("LabelCreatorTitle")->setEnabled(TRUE);
        getChildView("LabelCreatorName")->setEnabled(FALSE);
        getChild<LLUICtrl>("LabelCreatorName")->setValue(name);
    }
    else
    {
        getChildView("BtnCreator")->setEnabled(FALSE);
        getChildView("LabelCreatorTitle")->setEnabled(FALSE);
        getChildView("LabelCreatorName")->setEnabled(FALSE);
        getChild<LLUICtrl>("LabelCreatorName")->setValue(getString("unknown_multiple"));
    }

    ////////////////
    // OWNER NAME //
    ////////////////
    if (perm.isOwned())
    {
        std::string name;
        if (perm.isGroupOwned())
        {
            gCacheName->getGroupName(perm.getGroup(), name);
        }
        else
        {
            LLUUID owner_id = perm.getOwner();
            name = LLSLURL("agent", owner_id, "completename").getSLURLString();
        }
        getChildView("BtnOwner")->setEnabled(TRUE);
        getChildView("LabelOwnerTitle")->setEnabled(TRUE);
        getChildView("LabelOwnerName")->setEnabled(FALSE);
        getChild<LLUICtrl>("LabelOwnerName")->setValue(name);
    }
    else
    {
        getChildView("BtnOwner")->setEnabled(FALSE);
        getChildView("LabelOwnerTitle")->setEnabled(FALSE);
        getChildView("LabelOwnerName")->setEnabled(FALSE);
        getChild<LLUICtrl>("LabelOwnerName")->setValue(getString("public"));
    }

    ////////////
    // ORIGIN //
    ////////////

    // TODO: Whole path?

    /*if (object)
    {
        getChild<LLUICtrl>("origin")->setValue(getString("origin_inworld"));
    }
    else
    {
        getChild<LLUICtrl>("origin")->setValue(getString("origin_inventory"));
    }*/

    //////////////////
    // ACQUIRE DATE //
    //////////////////

    time_t time_utc = item->getCreationDate();
    if (0 == time_utc)
    {
        mAcquiredDateCtrl->setValue(getString("unknown"));
    }
    else
    {
        std::string timeStr = getString("acquiredDate");
        LLSD substitution;
        substitution["datetime"] = (S32)time_utc;
        LLStringUtil::format(timeStr, substitution);
        mAcquiredDateCtrl->setValue(timeStr);
    }

    //////////////////////////////////////
    // PERMISSIONS AND SALE ITEM HIDING //
    //////////////////////////////////////

    const std::string perm_and_sale_items[] = {
        "perms_inv",
        "perm_modify",
        "CheckOwnerModify",
        "CheckOwnerCopy",
        "CheckOwnerTransfer",
        "GroupLabel",
        "CheckShareWithGroup",
        "AnyoneLabel",
        "CheckEveryoneCopy",
        "NextOwnerLabel",
        "CheckNextOwnerModify",
        "CheckNextOwnerCopy",
        "CheckNextOwnerTransfer",
        "CheckPurchase",
        "ComboBoxSaleType",
        "Edit Cost"
    };

    const std::string debug_items[] = {
        "BaseMaskDebug",
        "OwnerMaskDebug",
        "GroupMaskDebug",
        "EveryoneMaskDebug",
        "NextMaskDebug"
    };

    // Hide permissions checkboxes and labels and for sale info if in the trash
    // or ui elements don't apply to these objects and return from function
    if (!not_in_trash || cannot_restrict_permissions)
    {
        for (size_t t = 0; t < LL_ARRAY_SIZE(perm_and_sale_items); ++t)
        {
            getChildView(perm_and_sale_items[t])->setVisible(false);
        }

        for (size_t t = 0; t < LL_ARRAY_SIZE(debug_items); ++t)
        {
            getChildView(debug_items[t])->setVisible(false);
        }
        return;
    }
    else // Make sure perms and sale ui elements are visible
    {
        for (size_t t = 0; t < LL_ARRAY_SIZE(perm_and_sale_items); ++t)
        {
            getChildView(perm_and_sale_items[t])->setVisible(true);
        }
    }

    ///////////////////////
    // OWNER PERMISSIONS //
    ///////////////////////

    U32 base_mask = perm.getMaskBase();
    U32 owner_mask = perm.getMaskOwner();
    U32 group_mask = perm.getMaskGroup();
    U32 everyone_mask = perm.getMaskEveryone();
    U32 next_owner_mask = perm.getMaskNextOwner();

    mOwnerModifyCtrl->setEnabled(FALSE);
    mOwnerModifyCtrl->setValue((BOOL)(owner_mask & PERM_MODIFY));
    mOwnerCopyCtrl->setEnabled(FALSE);
    mOwnerCopyCtrl->setValue((BOOL)(owner_mask & PERM_COPY));
    mOwnerTransfer->setEnabled(FALSE);
    mOwnerTransfer->setValue((BOOL)(owner_mask & PERM_TRANSFER));

    /////////////
    // SHARING //
    /////////////

    // Check for ability to change values.
    if (is_link || cannot_restrict_permissions)
    {
        mShareWithGroupCtrl->setEnabled(FALSE);
        mEveryoneCopyCtrl->setEnabled(FALSE);
    }
    else if (is_obj_modify && can_agent_manipulate)
    {
        mShareWithGroupCtrl->setEnabled(TRUE);
        mEveryoneCopyCtrl->setEnabled((owner_mask & PERM_COPY) && (owner_mask & PERM_TRANSFER));
    }
    else
    {
        mShareWithGroupCtrl->setEnabled(FALSE);
        mEveryoneCopyCtrl->setEnabled(FALSE);
    }

    // Set values.
    BOOL is_group_copy = (group_mask & PERM_COPY) ? TRUE : FALSE;
    BOOL is_group_modify = (group_mask & PERM_MODIFY) ? TRUE : FALSE;
    BOOL is_group_move = (group_mask & PERM_MOVE) ? TRUE : FALSE;

    if (is_group_copy && is_group_modify && is_group_move)
    {
        mShareWithGroupCtrl->setValue(TRUE);
        mShareWithGroupCtrl->setTentative(FALSE);
    }
    else if (!is_group_copy && !is_group_modify && !is_group_move)
    {
        mShareWithGroupCtrl->setValue(FALSE);
        mShareWithGroupCtrl->setTentative(FALSE);
    }
    else
    {
        mShareWithGroupCtrl->setTentative(!ctl->getEnabled());
        mShareWithGroupCtrl->set(TRUE);
    }

    mEveryoneCopyCtrl->setValue(everyone_mask & PERM_COPY);

    ///////////////
    // SALE INFO //
    ///////////////

    const LLSaleInfo& sale_info = item->getSaleInfo();
    BOOL is_for_sale = sale_info.isForSale();

    // Check for ability to change values.
    if (is_obj_modify && can_agent_sell
        && gAgent.allowOperation(PERM_TRANSFER, perm, GP_OBJECT_MANIPULATE))
    {
        mCheckPurchaseCtrl->setEnabled(is_complete);

        getChildView("NextOwnerLabel")->setEnabled(TRUE);
        mNextOwnerModifyCtrl->setEnabled((base_mask & PERM_MODIFY) && !cannot_restrict_permissions);
        mNextOwnerCopyCtrl->setEnabled((base_mask & PERM_COPY) && !cannot_restrict_permissions && !is_settings);
        mNextOwnerTransferCtrl->setEnabled((next_owner_mask & PERM_COPY) && !cannot_restrict_permissions);

        mSaleTypeCtrl->setEnabled(is_complete && is_for_sale);
        edit_cost->setEnabled(is_complete && is_for_sale);
    }
    else
    {
        mCheckPurchaseCtrl->setEnabled(FALSE);

        getChildView("NextOwnerLabel")->setEnabled(FALSE);
        mNextOwnerModifyCtrl->setEnabled(FALSE);
        mNextOwnerCopyCtrl->setEnabled(FALSE);
        mNextOwnerTransferCtrl->setEnabled(FALSE);

        mSaleTypeCtrl->setEnabled(FALSE);
        edit_cost->setEnabled(FALSE);
    }

    // Hide any properties that are not relevant to settings
    if (is_settings)
    {
        getChild<LLUICtrl>("GroupLabel")->setEnabled(false);
        getChild<LLUICtrl>("GroupLabel")->setVisible(false);
        mShareWithGroupCtrl->setEnabled(false);
        mShareWithGroupCtrl->setVisible(false);
        getChild<LLUICtrl>("AnyoneLabel")->setEnabled(false);
        getChild<LLUICtrl>("AnyoneLabel")->setVisible(false);
        mEveryoneCopyCtrl->setEnabled(false);
        mEveryoneCopyCtrl->setVisible(false);
        mCheckPurchaseCtrl->setEnabled(false);
        mCheckPurchaseCtrl->setVisible(false);
        mSaleTypeCtrl->setEnabled(false);
        mSaleTypeCtrl->setVisible(false);
        mCostCtrl->setEnabled(false);
        mCostCtrl->setVisible(false);
    }

    // Set values.
    mCheckPurchaseCtrl->setValue(is_for_sale);
    mNextOwnerModifyCtrl->setValue(BOOL(next_owner_mask & PERM_MODIFY));
    mNextOwnerCopyCtrl->setValue(BOOL(next_owner_mask & PERM_COPY));
    mNextOwnerTransferCtrl->setValue(BOOL(next_owner_mask & PERM_TRANSFER));

    if (is_for_sale)
    {
        S32 numerical_price;
        numerical_price = sale_info.getSalePrice();
        edit_cost->setValue(llformat("%d", numerical_price));
        combo_sale_type->setValue(sale_info.getSaleType());
    }
    else
    {
        edit_cost->setValue(llformat("%d", 0));
        combo_sale_type->setValue(LLSaleInfo::FS_COPY);
    }
}


void LLFloaterInventoryItemProperties::setAssociatedExperience(LLHandle<LLFloaterInventoryItemProperties> hInfo, const LLSD& experience)
{
    LLFloaterInventoryItemProperties* floater = hInfo.get();
    if (floater)
    {
        LLUUID id;
        if (experience.has(LLExperienceCache::EXPERIENCE_ID))
        {
            id = experience[LLExperienceCache::EXPERIENCE_ID].asUUID();
        }
        if (id.notNull())
        {
            floater->getChild<LLTextBox>("LabelItemExperience")->setText(LLSLURL("experience", id, "profile").getSLURLString());
        }
        else
        {
            floater->getChild<LLTextBox>("LabelItemExperience")->setText(LLTrans::getString("ExperienceNameNull"));
        }
    }
}


void LLFloaterInventoryItemProperties::startObjectInventoryObserver()
{
    if (!mObjectInventoryObserver)
    {
        stopObjectInventoryObserver();

        // Previous object observer should be removed before starting to observe a new object.
        llassert(mObjectInventoryObserver == NULL);
    }

    if (mObjectID.isNull())
    {
        LL_WARNS() << "Empty object id passed to inventory observer" << LL_ENDL;
        return;
    }

    LLViewerObject* object = gObjectList.findObject(mObjectID);

    mObjectInventoryObserver = new LLObjectInventoryObserver(this, object);
}

void LLFloaterInventoryItemProperties::stopObjectInventoryObserver()
{
    delete mObjectInventoryObserver;
    mObjectInventoryObserver = NULL;
}

void LLFloaterInventoryItemProperties::setPropertiesFieldsEnabled(bool enabled)
{
    const std::string fields[] = {
        "CheckOwnerModify",
        "CheckOwnerCopy",
        "CheckOwnerTransfer",
        "CheckShareWithGroup",
        "CheckEveryoneCopy",
        "CheckNextOwnerModify",
        "CheckNextOwnerCopy",
        "CheckNextOwnerTransfer",
        "CheckPurchase",
        "Edit Cost"
    };
    for (size_t t = 0; t < LL_ARRAY_SIZE(fields); ++t)
    {
        getChildView(fields[t])->setEnabled(false);
    }
}

// static
void LLFloaterInventoryItemProperties::onCommitName()
{
    //LL_INFOS() << "LLSidepanelItemInfo::onCommitName()" << LL_ENDL;
    LLViewerInventoryItem* item = findItem();
    if (!item)
    {
        return;
    }

    if (item->getName() != mItemNameCtrl->getText()) &&
        (gAgent.allowOperation(PERM_MODIFY, item->getPermissions(), GP_OBJECT_MANIPULATE))
    {
        LLPointer<LLViewerInventoryItem> new_item = new LLViewerInventoryItem(item);
        new_item->rename(mItemNameCtrl->getText());
        onCommitChanges(new_item);
    }
}

void LLFloaterInventoryItemProperties::onCommitDescription()
{
    //LL_INFOS() << "LLSidepanelItemInfo::onCommitDescription()" << LL_ENDL;
    LLViewerInventoryItem* item = findItem();
    if (!item) return;

    if ((item->getDescription() != mItemDescriptionCtrl->getText()) &&
        (gAgent.allowOperation(PERM_MODIFY, item->getPermissions(), GP_OBJECT_MANIPULATE)))
    {
        LLPointer<LLViewerInventoryItem> new_item = new LLViewerInventoryItem(item);

        new_item->setDescription(mItemDescriptionCtrl->getText());
        onCommitChanges(new_item);
    }
}

void LLFloaterInventoryItemProperties::onCommitPermissions(LLUICtrl* ctrl)
{
    if (ctrl)
    {
        // will be enabled by response from server
        ctrl->setEnabled(false);
    }
    updatePermissions();
}

void LLFloaterInventoryItemProperties::updatePermissions()
{
    LLViewerInventoryItem* item = findItem();
    if (!item) return;

    BOOL is_group_owned;
    LLUUID owner_id;
    LLUUID group_id;
    LLPermissions perm(item->getPermissions());
    perm.getOwnership(owner_id, is_group_owned);

    if (is_group_owned && gAgent.hasPowerInGroup(owner_id, GP_OBJECT_MANIPULATE))
    {
        group_id = owner_id;
    }

    perm.setGroupBits(gAgent.getID(), group_id, mShareWithGroupCtrl->get(), PERM_MODIFY | PERM_MOVE | PERM_COPY);
    perm.setEveryoneBits(gAgent.getID(), group_id, mEveryoneCopyCtrl->get(), PERM_COPY);
    perm.setNextOwnerBits(gAgent.getID(), group_id, mNextOwnerModifyCtrl->get(), PERM_MODIFY);
    perm.setNextOwnerBits(gAgent.getID(), group_id, mNextOwnerCopyCtrl->get(), PERM_COPY);
    perm.setNextOwnerBits(gAgent.getID(), group_id, mNextOwnerTransferCtrl->get(), PERM_TRANSFER);

    if (perm != item->getPermissions()
        && item->isFinished())
    {
        LLPointer<LLViewerInventoryItem> new_item = new LLViewerInventoryItem(item);
        new_item->setPermissions(perm);
        U32 flags = new_item->getFlags();
        // If next owner permissions have changed (and this is an object)
        // then set the slam permissions flag so that they are applied on rez.
        if ((perm.getMaskNextOwner() != item->getPermissions().getMaskNextOwner())
            && (item->getType() == LLAssetType::AT_OBJECT))
        {
            flags |= LLInventoryItemFlags::II_FLAGS_OBJECT_SLAM_PERM;
        }
        // If everyone permissions have changed (and this is an object)
        // then set the overwrite everyone permissions flag so they
        // are applied on rez.
        if ((perm.getMaskEveryone() != item->getPermissions().getMaskEveryone())
            && (item->getType() == LLAssetType::AT_OBJECT))
        {
            flags |= LLInventoryItemFlags::II_FLAGS_OBJECT_PERM_OVERWRITE_EVERYONE;
        }
        // If group permissions have changed (and this is an object)
        // then set the overwrite group permissions flag so they
        // are applied on rez.
        if ((perm.getMaskGroup() != item->getPermissions().getMaskGroup())
            && (item->getType() == LLAssetType::AT_OBJECT))
        {
            flags |= LLInventoryItemFlags::II_FLAGS_OBJECT_PERM_OVERWRITE_GROUP;
        }
        new_item->setFlags(flags);
        onCommitChanges(new_item);
    }
    else
    {
        // need to make sure we don't just follow the click
        refresh();
    }
}

// static
void LLFloaterInventoryItemProperties::onCommitSaleInfo(LLUICtrl* ctrl)
{
    if (ctrl)
    {
        // will be enabled by response from server
        ctrl->setEnabled(false);
    }
    //LL_INFOS() << "LLSidepanelItemInfo::onCommitSaleInfo()" << LL_ENDL;
    updateSaleInfo();
}

void LLFloaterInventoryItemProperties::updateSaleInfo()
{
    LLViewerInventoryItem* item = findItem();
    if (!item) return;
    LLSaleInfo sale_info(item->getSaleInfo());
    if (!gAgent.allowOperation(PERM_TRANSFER, item->getPermissions(), GP_OBJECT_SET_SALE))
    {
        mCheckPurchaseCtrl->setValue(LLSD((BOOL)FALSE));
    }

    if (mCheckPurchaseCtrl->getValue().asBOOL())
    {
        // turn on sale info
        LLSaleInfo::EForSale sale_type = static_cast<LLSaleInfo::EForSale>(mSaleTypeCtrl->getValue().asInteger());

        if (sale_type == LLSaleInfo::FS_COPY
            && !gAgent.allowOperation(PERM_COPY, item->getPermissions(),
                GP_OBJECT_SET_SALE))
        {
            sale_type = LLSaleInfo::FS_ORIGINAL;
        }



        S32 price = -1;
        price = mCostCtrl->getValue().asInteger();;

        // Invalid data - turn off the sale
        if (price < 0)
        {
            sale_type = LLSaleInfo::FS_NOT;
            price = 0;
        }

        sale_info.setSaleType(sale_type);
        sale_info.setSalePrice(price);
    }
    else
    {
        sale_info.setSaleType(LLSaleInfo::FS_NOT);
    }
    if (sale_info != item->getSaleInfo()
        && item->isFinished())
    {
        LLPointer<LLViewerInventoryItem> new_item = new LLViewerInventoryItem(item);

        // Force an update on the sale price at rez
        if (item->getType() == LLAssetType::AT_OBJECT)
        {
            U32 flags = new_item->getFlags();
            flags |= LLInventoryItemFlags::II_FLAGS_OBJECT_SLAM_SALE;
            new_item->setFlags(flags);
        }

        new_item->setSaleInfo(sale_info);
        onCommitChanges(new_item);
    }
    else
    {
        // need to make sure we don't just follow the click
        refresh();
    }
}

void LLFloaterInventoryItemProperties::onCommitChanges(LLPointer<LLViewerInventoryItem> item)
{
    if (item.isNull())
    {
        return;
    }

    if (mObjectID.isNull())
    {
        // This is in the agent's inventory.
        // Mark update as pending and wait only for most recent one in case user requested for couple
        // Once update arrives or any of ids change drop pending id.
        mUpdatePendingId++;
        LLPointer<LLInventoryCallback> callback = new PropertiesChangedCallback(getHandle(), mItemID, mUpdatePendingId);
        update_inventory_item(item.get(), callback);
        //item->updateServer(FALSE);
        gInventory.updateItem(item);
        gInventory.notifyObservers();
    }
    else
    {
        // This is in an object's contents.
        LLViewerObject* object = gObjectList.findObject(mObjectID);
        if (object)
        {
            object->updateInventory(
                item,
                TASK_INVENTORY_ITEM_KEY,
                false);

            if (object->isSelected())
            {
                // Since object is selected (build floater is open) object will
                // receive properties update, detect serial mismatch, dirty and
                // reload inventory, meanwhile some other updates will refresh it.
                // So mark dirty early, this will prevent unnecessary changes
                // and download will be triggered by LLPanelObjectInventory - it
                // prevents flashing in content tab and some duplicated request.
                object->dirtyInventory();
            }
            setPropertiesFieldsEnabled(false);
        }
    }
}

LLViewerInventoryItem* LLFloaterInventoryItemProperties::findItem() const
{
    LLViewerInventoryItem* item = NULL;
    if (mObjectID.isNull())
    {
        // it is in agent inventory
        item = gInventory.getItem(mItemID);
    }
    else
    {
        LLViewerObject* object = gObjectList.findObject(mObjectID);
        if (object)
        {
            item = static_cast<LLViewerInventoryItem*>(object->getInventoryObject(mItemID));
        }
    }
    return item;
}

// virtual
void LLFloaterInventoryItemProperties::save()
{
    onCommitName();
    onCommitDescription();
    updatePermissions();
    updateSaleInfo();
}

