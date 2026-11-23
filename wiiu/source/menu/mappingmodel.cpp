/****************************************************************************
 * not64 for Wii U
 * Based on Snes9x GX, Daryl Borth 2026
 * mappingmodel.cpp
 *
 * See mappingmodel.h.
 ***************************************************************************/

#include "mappingmodel.h"
#include "button_mapping.h"
#include "input.h"

int MapProfile::rowCount() const
{
	int n = 0;
	for(size_t g = 0; g < left.size(); g++)
		n += (int)left[g].rows.size();
	for(size_t g = 0; g < right.size(); g++)
		n += (int)right[g].rows.size();
	return n;
}

// The indexes are the N64_BTN_* values of input.h (and so the ones preferences.xml and ResetControls use)
static MapProfile BuildPad()
{
	MapProfile p;
	p.left.push_back({ "SHOULDER", { { "L", N64_BTN_L, MAP_ICON::L }, { "R", N64_BTN_R, MAP_ICON::R }, { "Z", N64_BTN_Z, MAP_ICON::NONE } } });
	p.left.push_back({ "D-PAD", { { "Up", N64_BTN_D_UP, MAP_ICON::UP }, { "Down", N64_BTN_D_DOWN, MAP_ICON::DOWN }, { "Left", N64_BTN_D_LEFT, MAP_ICON::LEFT }, { "Right", N64_BTN_D_RIGHT, MAP_ICON::RIGHT } } });
	p.right.push_back({ "FACE BUTTONS", { { "A", N64_BTN_A, MAP_ICON::A }, { "B", N64_BTN_B, MAP_ICON::B } } });
	p.right.push_back({ "C BUTTONS", { { "C Up", N64_BTN_C_UP, MAP_ICON::NONE }, { "C Down", N64_BTN_C_DOWN, MAP_ICON::NONE }, { "C Left", N64_BTN_C_LEFT, MAP_ICON::NONE }, { "C Right", N64_BTN_C_RIGHT, MAP_ICON::NONE } } });
	p.right.push_back({ "SYSTEM", { { "Start", N64_BTN_START, MAP_ICON::START } } });
	return p;
}

const MapProfile& MappingProfile(int ctrl)
{
	static const MapProfile pad = BuildPad();
	static const MapProfile none;

	if(ctrl == CTRL_PAD)
		return pad;
	return none;
}

const ControllerLayout& MappingLayout(int hw, bool sideways)
{
	switch(hw)
	{
		case INPUT_HW_WIIMOTE: return sideways ? WiimoteSidewaysControllerLayout() : WiimoteControllerLayout();
		case INPUT_HW_NUNCHUK: return WiimoteNunchukControllerLayout();
		case INPUT_HW_CLASSIC: return ClassicControllerLayout();
		case INPUT_HW_WUPC: return WiiUProControllerLayout();
		case INPUT_HW_DRC: return GamePadControllerLayout();
		case INPUT_HW_DS4: return DS4ControllerLayout();
		case INPUT_HW_XBOX360: return Xbox360ControllerLayout();
		case INPUT_HW_GENERIC_USB: return UsbControllerLayout();
		default: break;
	}
	return GameCubeControllerLayout();
}

float MappingScale(const ControllerLayout& layout, float maxW, float maxH)
{
	if(layout.width <= 0.0f || layout.height <= 0.0f)
		return 1.0f;
	const float sw = maxW / layout.width;
	const float sh = maxH / layout.height;
	return sw < sh ? sw : sh;
}

const char * MappingButtonName(int hw, uint32_t btn)
{
	if(btn == 0 || hw < 0 || hw >= (int)INPUT_HW_MAX)
		return nullptr;
	const CtrlrMap& def = ctrlr_def[hw];
	for(int i = 0; i < def.num_btns && i < 15; i++)
		if(def.map[i].btn == btn)
			return def.map[i].name;
	return nullptr;
}

int MappingSpotFor(const ControllerLayout& layout, uint32_t btn)
{
	if(btn == 0)
		return 0;
	return layout.find((int)btn) ? (int)btn : 0;
}
