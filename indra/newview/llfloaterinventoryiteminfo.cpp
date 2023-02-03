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


//-----------------------------------------------------------------------------
// LLFloaterItemProperties
//-----------------------------------------------------------------------------

LLFloaterItemProperties::LLFloaterItemProperties(const LLSD& key)
: LLFloater(key)
{
}

LLFloaterItemProperties::~LLFloaterItemProperties()
{
}

BOOL LLFloaterItemProperties::postBuild()
{
    // On the standalone properties floater, we have no need for a back button...
    /*LLSidepanelItemInfo* panel = getChild<LLSidepanelItemInfo>("item_panel");
    LLButton* back_btn = panel->getChild<LLButton>("back_btn");
    back_btn->setVisible(FALSE);*/
    
    return LLFloater::postBuild();
}

void LLFloaterItemProperties::onOpen(const LLSD& key)
{
    // Tell the panel which item it needs to visualize
    /*LLSidepanelItemInfo* panel = getChild<LLSidepanelItemInfo>("item_panel");
    panel->setItemID(key["id"].asUUID());*/
}

