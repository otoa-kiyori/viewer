/** 
 * @file llfloaterinventoryiteminfo.h
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

#ifndef LL_LLFLOATERINVENTORYITEMPROPERTIES_H
#define LL_LLFLOATERINVENTORYITEMPROPERTIES_H

#include "llfloater.h"

#include "llinventoryobserver.h"

class LLViewerInventoryItem;
class LLItemPropertiesObserver;
class LLObjectInventoryObserver;
class LLViewerObject;

class LLComboBox;
class LLCheckBoxCtrl;
class LLLineEditor;
class LLTextBox;

//-----------------------------------------------------------------------------
// LLFloaterItemProperties
//-----------------------------------------------------------------------------

class LLFloaterInventoryItemProperties : public LLFloater, public LLInventoryObserver
{
public:
    LLFloaterInventoryItemProperties(const LLSD& key);
    virtual ~LLFloaterInventoryItemProperties();

    BOOL postBuild() override;
    virtual void onOpen(const LLSD& key) override;

    // if received update and item id (from callback) matches internal ones, update UI
    void onUpdateCallback(const LLUUID& item_id, S32 received_update_id);

private:
    void setObjectID(const LLUUID& object_id);
    void setItemID(const LLUUID& item_id);
    const LLUUID& getObjectID() const;
    const LLUUID& getItemID() const;

protected:
    /*virtual*/ void refresh();
    /*virtual*/ void save();

    LLViewerInventoryItem* findItem() const;
    LLViewerObject*  findObject() const;

    void refreshFromItem(LLViewerInventoryItem* item);

    void onCommitName();
    void onCommitDescription();
    void onCommitPermissions(LLUICtrl* ctrl);
    void updatePermissions();
    void onCommitSaleInfo(LLUICtrl* ctrl);
    void updateSaleInfo();
    void onCommitChanges(LLPointer<LLViewerInventoryItem> item);

private:
    static void setAssociatedExperience(LLHandle<LLFloaterInventoryItemProperties> floater, const LLSD& experience);

    void startObjectInventoryObserver();
    void stopObjectInventoryObserver();
    void setPropertiesFieldsEnabled(bool enabled);

    LLUUID mItemID; 	// inventory UUID for the inventory item.
    LLUUID mObjectID; 	// in-world task UUID, or null if in agent inventory.
    LLItemPropertiesObserver* mPropertiesObserver; // for syncing changes to item
    LLObjectInventoryObserver* mObjectInventoryObserver; // for syncing changes to items inside an object

    // We can send multiple properties updates simultaneously, make sure only last response counts and there won't be a race condition.
    S32 mUpdatePendingId;

    LLLineEditor* mItemNameCtrl;
    LLLineEditor* mItemDescriptionCtrl;
    LLTextBox* mItemExperienceNameCtrl;
    LLUICtrl* mItemExperienceTitleCtrl;
    LLUICtrl* mAcquiredDateCtrl;
    LLUICtrl* mIconLockedCtrl;

    LLCheckBoxCtrl* mOwnerModifyCtrl;
    LLCheckBoxCtrl* mOwnerCopyCtrl;
    LLCheckBoxCtrl* mOwnerTransferCtrl;
    LLCheckBoxCtrl* mShareWithGroupCtrl;
    LLCheckBoxCtrl* mEveryoneCopyCtrl;
    LLCheckBoxCtrl* mNextOwnerModifyCtrl;
    LLCheckBoxCtrl* mNextOwnerCopyCtrl;
    LLCheckBoxCtrl* mNextOwnerTransferCtrl;
    LLCheckBoxCtrl* mCheckPurchaseCtrl;
    LLComboBox* mSaleTypeCtrl;
    LLUICtrl* mCostCtrl;
};

#endif // LL_LLFLOATERINVENTORYITEMPROPERTIES_H
