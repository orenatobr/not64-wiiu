/****************************************************************************
 * TestInputScript.h
 *
 * Scripted GamePad input for emulator smoke tests (Cemu, CI). Only compiled in
 * when the build defines NOT64_TEST_INPUT, so release builds never read it.
 *
 * The script is a text file, sd:/not64/test_input.txt, one step per line:
 *
 *     <first frame> <frames held> <BUTTON>
 *
 * BUTTON is one of A B X Y L R ZL ZR PLUS MINUS UP DOWN LEFT RIGHT HOME. Frames
 * are counted from the first menu update. Lines starting with # are ignored.
 ***************************************************************************/
#pragma once

#ifdef NOT64_TEST_INPUT

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vpad/input.h>

class TestInputScript {
public:
	//! Loads the script from dir (a mount path that ends in "/"). Safe to call twice.
	void load(const char * dir)
	{
		if (loaded) return;
		loaded = true;
		char path[256];
		snprintf(path, sizeof(path), "%snot64/test_input.txt", dir);
		FILE * f = fopen(path, "r");
		if (!f) return;
		char line[96];
		while (count < MAX_STEPS && fgets(line, sizeof(line), f)) {
			unsigned first = 0, len = 0;
			char name[16] = { 0 };
			if (line[0] == '#' || sscanf(line, "%u %u %15s", &first, &len, name) != 3) continue;
			uint32_t mask = buttonFromName(name);
			if (mask) steps[count++] = { first, len, mask };
		}
		fclose(f);
	}

	//! Buttons held on the current frame; call once per update.
	uint32_t nextHold()
	{
		uint32_t hold = 0;
		for (int i = 0; i < count; i++) {
			if (frame >= steps[i].first && frame < steps[i].first + steps[i].len) hold |= steps[i].mask;
		}
		frame++;
		return hold;
	}

	bool active() const { return count > 0; }

private:
	struct Step { unsigned first, len; uint32_t mask; };
	static const int MAX_STEPS = 256;
	Step steps[MAX_STEPS];
	int count = 0;
	unsigned frame = 0;
	bool loaded = false;

	static uint32_t buttonFromName(const char * n)
	{
		static const struct { const char * name; uint32_t mask; } table[] = {
			{ "A", VPAD_BUTTON_A }, { "B", VPAD_BUTTON_B }, { "X", VPAD_BUTTON_X }, { "Y", VPAD_BUTTON_Y },
			{ "L", VPAD_BUTTON_L }, { "R", VPAD_BUTTON_R }, { "ZL", VPAD_BUTTON_ZL }, { "ZR", VPAD_BUTTON_ZR },
			{ "PLUS", VPAD_BUTTON_PLUS }, { "MINUS", VPAD_BUTTON_MINUS }, { "HOME", VPAD_BUTTON_HOME },
			{ "UP", VPAD_BUTTON_UP }, { "DOWN", VPAD_BUTTON_DOWN }, { "LEFT", VPAD_BUTTON_LEFT }, { "RIGHT", VPAD_BUTTON_RIGHT },
		};
		for (const auto & e : table) {
			if (strcmp(e.name, n) == 0) return e.mask;
		}
		return 0;
	}
};

#endif
