/****************************************************************************
 * not64 for Wii U
 *
 * Based on Snes9x GX, Daryl Borth 2008-2026
 *
 * Daryl Borth 2008-2026
 *
 * menu.cpp
 *
 * Menu flow routines - handles all menu logic
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <new>

#ifdef HW_RVL
#include <ogc/ios.h>
#endif

#include "not64gx.h"
#include "emu_n64.h"
#include "memmanager.h"
#include "video.h"
#include "filebrowser.h"
#include "utils/decompress.h"
#include "fileop.h"
#include "savemode.h"
#include "preferences.h"
#include "button_mapping.h"
#include "input.h"
#include "menu.h"
#include "mappingui.h"
#include "menupopover.h"
#include "filelist.h"
#include "libgui/Gui.h"
#include "widgets/GuiWidgets.h"
#include "menuart.h"
#include "controllerstatus.h"
#include "utils/pngcodec.h"
#include "drivers/Time.h"
#include "drivers/Thread.h"
#include "drivers/Mutex.h"
#include "drivers/Cond.h"
#include "drivers/safestr.h"
#include "drivers/wut/WutInputDriver.h"
#include "drivers/wut/WutUpscaleFilters.h"

#define THREAD_SLEEP 100

#ifndef HW_DOL
static GuiImageData * pointer[4];
static GuiImage cursorImg[4];
#endif

static GuiTrigger * trigA = nullptr;
static void * gameScreenTexture = nullptr;
static GuiElement * gameScreenImg = nullptr; // blurred game screenshot, or the plain page background
static GuiSound * bgMusic = nullptr;
static GuiSound * enterSound = nullptr;
static GuiSound * exitSound = nullptr;
static GuiText * settingText = nullptr;
static GuiText * settingText2 = nullptr;
static int lastMenu = MENU_NONE;
static int previousMenu = MENU_NONE; // the page that ran just before the current one (MENU_NONE: the game)

static int currentLanguage = -1;

uint8_t * bg_music;
uint32_t bg_music_size;

struct Menu;
static Menu * menu = nullptr;

static volatile int showProgress = 0;

// ProgressSync().idleCond signals when the overlay has been fully torn
// down; ProgressSync().workCond is unused - see ThreadSync in drivers/Thread.h.
static ThreadSync & ProgressSync() { static ThreadSync s; return s; }
static bool progIdle = true; // protected by ProgressSync().mutex - true when no overlay is showing/pending

static char progressTitle[101];
static char progressMsg[201];
static int progressDone = 0;
static int progressTotal = 0;
static void (*progressCancelFn)(void) = nullptr; // protected by ProgressSync().mutex - non-null means the overlay offers a Cancel button

static ThreadId mainThreadId;

static bool IsMainThread()
{
	return ThreadId::current() == mainThreadId;
}

// PromptSync().idleCond signals main -> background: result is ready;
// PromptSync().workCond is unused.
static ThreadSync & PromptSync() { static ThreadSync s; return s; }
static bool promptPending      = false; // protected by PromptSync().mutex
static bool promptResultReady  = false; // protected by PromptSync().mutex
static int     promptResult       = 0;
static const char * promptPendingTitle;
static const char * promptPendingMsg;
static const char * promptPendingBtn1;
static const char * promptPendingBtn2;

/****************************************************************************
 * InitGUIThreads
 *
 * Startup GUI threads
 ***************************************************************************/
void
InitGUIThreads()
{
	mainThreadId = ThreadId::current();

	// Force ProgressSync()/PromptSync()'s function-local statics to
	// construct now rather than on first use from a background thread.
	ProgressSync();
	PromptSync();
}

/****************************************************************************
 * UpdateProgressOverlay
 *
 * Reflects the current progress/action state into the GUI
 ***************************************************************************/
struct ProgressOverlayState {
	GuiWindow progressWindow;
	MenuDialogShape dialogBoxImg;
	MenuProgressBar progressBar;
	MenuSpinner spinner;
	GuiText titleTxt;
	GuiText msgTxt;
	MenuButtonShape cancelImg;
	GuiText cancelTxt;
	GuiButton cancelBtn;
	GuiTrigger cancelTrigB;

	bool cancelShown;
	bool overlayShown;
	bool waitingToShow;
	Ticks pendingStart;
	STATE oldState;
	float angle;
	uint32_t count;

	ProgressOverlayState() :
		progressWindow(448, 288),
		dialogBoxImg(MENU_DIALOG_W, MENU_DIALOG_H, MENU_DIALOG_HEADER_H),
		titleTxt(nullptr, 26, (PixelColor){255, 255, 255, 255}),
		msgTxt(nullptr, 26, (PixelColor){0, 0, 0, 255}),
		cancelImg(MenuButtonShape::NORMAL),
		cancelTxt("Cancel", 22, (PixelColor){0, 0, 0, 255}),
		cancelBtn(MENU_BTN_NORMAL_W, MENU_BTN_NORMAL_H),
		cancelShown(false),
		overlayShown(false), waitingToShow(false), pendingStart(0),
		oldState(STATE::DEFAULT), angle(0), count(0)
	{
		progressWindow.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
		progressWindow.setPosition(0, -10);
		progressWindow.append(&dialogBoxImg);

		titleTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		titleTxt.setPosition(0, MENU_DIALOG_TITLE_Y);
		progressWindow.append(&titleTxt);

		msgTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
		msgTxt.setPosition(0, 80);
		progressWindow.append(&msgTxt);

		progressBar.setAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
		progressBar.setPosition((MENU_DIALOG_W - MENU_PROGRESS_W) / 2, 40);

		spinner.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
		spinner.setPosition(0, 40);

		cancelBtn.setAlignment(ALIGN_H::CENTRE, ALIGN_V::BOTTOM);
		cancelBtn.setPosition(0, -25);
		cancelBtn.setLabel(&cancelTxt);
		cancelBtn.setImage(&cancelImg);
		cancelTrigB.setSecondaryTrigger();
	}

	void update();
};

struct Menu {
	GuiWindow mainWindow;

	MenuBarShape bgTopImg;
	MenuBarShape bgBottomImg;
	GuiImageData logo;
	GuiImage logoImg;
	GuiImageData logoOver;
	GuiImage logoImgOver;
	GuiText logoTxt;
	GuiButton btnLogo;

	ProgressOverlayState progressOverlayState;

	Menu() :
		mainWindow(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight()),
		bgTopImg(MenuBarShape::TOP),
		bgBottomImg(MenuBarShape::BOTTOM),
		logo(logo_png), logoImg(&logo),
		logoOver(logo_over_png), logoImgOver(&logoOver),
		logoTxt(APPVERSION, 18, (PixelColor){255, 255, 255, 255}),
		btnLogo(logoImg.getWidth(), logoImg.getHeight())
	{
		logoTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);

		btnLogo.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
		btnLogo.setPosition(-50, 24);
		btnLogo.setImage(&logoImg);
		btnLogo.setImageOver(&logoImgOver);
		btnLogo.setLabel(&logoTxt);
		btnLogo.setTrigger(trigA);

		mainWindow.append(gameScreenImg);
		mainWindow.append(&bgTopImg);
		mainWindow.append(&bgBottomImg);
		mainWindow.append(&btnLogo);
	}
};

void ProgressOverlayState::update() {
	if(!menu) return;

	ProgressSync().mutex.lock();
	int progress = showProgress;
	int done = progressDone;
	int total = progressTotal;
	void (*cancelFn)(void) = progressCancelFn;
	char title[101]; snprintf(title, sizeof(title), "%s", progressTitle);
	char msg[201]; snprintf(msg, sizeof(msg), "%s", progressMsg);
	ProgressSync().mutex.unlock();

	if(!progress)
	{
		waitingToShow = false;

		if(overlayShown)
		{
			menu->mainWindow.remove(&progressWindow);
			menu->mainWindow.setState(oldState);
			overlayShown = false;
			cancelShown = false;
		}

		ProgressSync().mutex.lock();
		if(!progIdle)
		{
			progIdle = true;
			ProgressSync().idleCond.signal();
		}
		ProgressSync().mutex.unlock();
	}
	else if(!overlayShown)
	{
		if(!waitingToShow)
		{
			waitingToShow = true;
			pendingStart = SystemTime::now();
		}
		else if(SystemTime::diffMillisecs(pendingStart, SystemTime::now()) >= 400)
		{
			titleTxt.setText(title);
			msgTxt.setText(msg);

			progressWindow.remove(&progressBar);
			progressWindow.remove(&spinner);
			progressWindow.remove(&cancelBtn);
			spinner.setPosition(0, 40);

			if(progress == 1)
			{
				progressBar.setProgress(0);
				progressWindow.append(&progressBar);
			}
			else
			{
				progressWindow.append(&spinner);

				if(cancelFn)
				{
					// Move the throbber up so the button fits below
					spinner.setPosition(0, 15);
					cancelBtn.setTrigger(0, trigA);
					cancelBtn.setTrigger(1, &cancelTrigB);
					cancelBtn.setState(STATE::SELECTED);
					progressWindow.append(&cancelBtn);
					cancelShown = true;
				}
			}

			oldState = menu->mainWindow.getState();
			menu->mainWindow.setState(STATE::DISABLED);
			menu->mainWindow.append(&progressWindow);
			menu->mainWindow.changeFocus(&progressWindow);

			overlayShown = true;
			waitingToShow = false;
			angle = 0;
			count = 0;
		}
	}

	if(overlayShown)
	{
		if(progress == 1 && total > 0)
		{
			progressBar.setProgress((int)(100*done/total));
		}
		else if(progress == 2)
		{
			if(count % 5 == 0)
			{
				angle += 45.0f;
				if(angle >= 360.0f)
					angle = 0;
				spinner.setPhase((int)(angle / 45.0f));
			}
			++count;
		}

		if(cancelShown && cancelBtn.getState() == STATE::CLICKED)
		{
			cancelBtn.resetState();
			progressWindow.remove(&cancelBtn);
			cancelShown = false;
			msgTxt.setText("Cancelling...");

			if(cancelFn)
				cancelFn();
		}
	}
}

// Held while a task runs on the worker thread: nothing on the page reacts to input, but the page is not dimmed
static int guiInputBlocked = 0;

struct GuiInputBlock
{
	GuiInputBlock() { guiInputBlocked++; }
	~GuiInputBlock() { guiInputBlocked--; }
	GuiInputBlock(const GuiInputBlock &) = delete;
	GuiInputBlock & operator=(const GuiInputBlock &) = delete;
};

static void ProcessInputData() {
	platform->getInput()->update();

	// the progress overlay, once it is up, is the one thing that still reacts (its Cancel button)
	if(guiInputBlocked > 0 && !menu->progressOverlayState.overlayShown)
		return;

	for(int i = 3; i >= 0; i--)
		menu->mainWindow.update(controller[i]);
}

static void DrawGui() {
	menu->mainWindow.draw();

	#ifndef HW_DOL
	for(int i = 3; i >= 0; i--)
	{
		if(controller[i]->getPadData().validPointer) {
			cursorImg[i].setPosition(controller[i]->getPadData().cursor_x - cursorImg[i].getWidth()/2, controller[i]->getPadData().cursor_y - cursorImg[i].getHeight()/2);
			cursorImg[i].setAngle(controller[i]->getPadData().cursor_angle);
			cursorImg[i].draw();
		}
	}
	#endif

	platform->getVideo()->renderMenu();
}

/****************************************************************************
 * CreditsWindow
 * Display credits, legal copyright and licence
 *
 * THIS MUST NOT BE REMOVED OR DISABLED IN ANY DERIVATIVE WORK
 ***************************************************************************/

static bool UpdateGui();

static void CreditsWindow()
{
	int i = 0;
	int y = 14;

	GuiWindow creditsWindowBox(MENU_CREDITS_W, MENU_CREDITS_H);
	creditsWindowBox.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);

	MenuDialogShape creditsBoxImg(MENU_CREDITS_W, MENU_CREDITS_H, MENU_CREDITS_HEADER_H);
	creditsWindowBox.append(&creditsBoxImg);

	// the lines: left column, right column, vertical advance after the pair
	struct CreditLine { const char * left; const char * right; int advance; };
	static const CreditLine lines[] = {
		{ "Wii U port, GX2 renderer",     "orenatobr",            24 },
		{ "Menu & Wii U drivers",         "Daryl Borth (dborth)", 24 },
		{ "Menu sound",                   "Peter de Man",         48 },
		{ "not64 / Wii64 / Mupen64GC",    "Extrems, tehpola,",    24 },
		{ nullptr,                        "sepp256, emu_kidid",   24 },
		{ "glN64",                        "Orkin",                24 },
		{ "Mupen64",                      "Hacktarux",            24 },
		{ "wut / devkitPPC",              "Wii U toolchain devs", 24 },
	};

	int numEntries = 1 /*title*/ + 1 /*site*/ + 3 /*console info*/ + 3 /*license*/;
	for(const CreditLine & l : lines)
		numEntries += (l.left ? 1 : 0) + 1;

	GuiText * txt[numEntries];

	// the title sits on the dialog's chrome band, so it is white; the credits start below the band
	txt[i] = new GuiText("Credits", 30, (PixelColor){255, 255, 255, 255});
	txt[i]->setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP); txt[i]->setPosition(0, MENU_CREDITS_TITLE_Y); i++; y+=56;

	txt[i] = new GuiText("Official Site: https://github.com/orenatobr/not64-wiiu", 20, (PixelColor){0, 0, 0, 255});
	txt[i]->setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP); txt[i]->setPosition(0,y); i++; y+=40;

	GuiText::setPresets(20, (PixelColor){0, 0, 0, 255}, 0, GUI_TEXT_JUSTIFY_LEFT | GUI_TEXT_ALIGN_TOP, ALIGN_H::LEFT, ALIGN_V::TOP);
	for(const CreditLine & l : lines)
	{
		if(l.left)
		{
			txt[i] = new GuiText(l.left);
			txt[i]->setPosition(60,y); i++;
		}
		txt[i] = new GuiText(l.right);
		txt[i]->setPosition(350,y); i++; y+=l.advance;
	}

	char consoleDetails[40];
	char memoryFreeInfo[50];
	char controllerInfo[128] = { 0 };

	snprintf(consoleDetails, sizeof(consoleDetails), "%s", platform->getConsoleDetails());
	snprintf(memoryFreeInfo, sizeof(memoryFreeInfo), "%s", platform->getMemoryFreeInfo());

	snprintf(controllerInfo, sizeof(controllerInfo), "%s", GetUSBControllerInfo());

	txt[i] = new GuiText(nullptr, 14, (PixelColor){0, 0, 0, 255});
	txt[i]->setRawText(consoleDetails);
	txt[i]->setAlignment(ALIGN_H::RIGHT, ALIGN_V::BOTTOM);
	txt[i]->setPosition(-20,-90); i++;
	txt[i] = new GuiText(nullptr, 14, (PixelColor){0, 0, 0, 255});
	txt[i]->setRawText(memoryFreeInfo);
	txt[i]->setAlignment(ALIGN_H::RIGHT, ALIGN_V::BOTTOM);
	txt[i]->setPosition(-20,-76); i++;
	txt[i] = new GuiText(controllerInfo, 14, (PixelColor){0, 0, 0, 255});
	txt[i]->setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
	txt[i]->setPosition(20,-52); i++;

	GuiText::setPresets(12, (PixelColor){0, 0, 0, 255}, 0, GUI_TEXT_JUSTIFY_CENTER | GUI_TEXT_ALIGN_TOP, ALIGN_H::CENTRE, ALIGN_V::BOTTOM);

	txt[i] = new GuiText("not64 - Copyright (c) Wii64 team and Extrems, Mupen64 - Copyright (c) Hacktarux");
	txt[i]->setPosition(0,-44); i++;
	txt[i] = new GuiText("This software is open source and may be copied, distributed, or modified ");
	txt[i]->setPosition(0,-32); i++;
	txt[i] = new GuiText("under the terms of the GNU General Public License (GPL) Version 2.");
	txt[i]->setPosition(0,-20);

	for(i=0; i < numEntries; i++)
		creditsWindowBox.append(txt[i]);

	STATE oldState = menu->mainWindow.getState();
	menu->mainWindow.setState(STATE::DISABLED);
	menu->mainWindow.append(&creditsWindowBox);
	menu->mainWindow.changeFocus(&creditsWindowBox);

	auto buttonPressed = [&]()-> bool {
		return controller[0]->getPadData().buttons_d || controller[1]->getPadData().buttons_d ||
			   controller[2]->getPadData().buttons_d || controller[3]->getPadData().buttons_d; };

	bool running = true;

	// debounce - wait for button to be unpressed
	while(running && buttonPressed())
		running = UpdateGui();

	// credits open - wait for button to be pressed
	while(running && !buttonPressed())
		running = UpdateGui();

	menu->mainWindow.remove(&creditsWindowBox);

	for(i=0; i < numEntries; i++)
		delete txt[i];

	// credits closed - wait for button to be unpressed (so we don't just reopen credits)
	while(running && buttonPressed())
		running = UpdateGui();

	menu->mainWindow.setState(oldState);
}

static void ServicePendingCreditsWindowRequest() {
	if(menu->btnLogo.getState() == STATE::CLICKED)
	{
		menu->btnLogo.resetState();
		CreditsWindow();
	}
}

/****************************************************************************
 * Cross-thread WindowPrompt() support
 ***************************************************************************/
static int WindowPrompt(const char *, const char *, const char *, const char *);

static void ServicePendingWindowPromptRequest()
{
	PromptSync().mutex.lock();
	bool pending = promptPending;
	const char * title = promptPendingTitle;
	const char * msg = promptPendingMsg;
	const char * btn1 = promptPendingBtn1;
	const char * btn2 = promptPendingBtn2;
	// Reentrant call handling - clear promptPending now, before the blocking WindowPrompt() call below
	if(pending)
		promptPending = false;
	PromptSync().mutex.unlock();

	if(!pending)
		return;

	int result = WindowPrompt(title, msg, btn1, btn2);

	PromptSync().mutex.lock();
	promptResult = result;
	promptResultReady = true;
	PromptSync().idleCond.signal();
	PromptSync().mutex.unlock();
}

/****************************************************************************
 * UpdateGui
 *
 * The single shared frame-step primitive. Performs exactly one frame:
 * scans input for all active controllers, applies that input to the
 * element tree, reflects the progress overlay state, draws the tree, and
 * presents/renders
 *
 * Returns true if the caller should keep looping, or false if the caller
 * must stop immediately and return control up the call stack
 ***************************************************************************/

// Set once the OS has asked the app to quit and the fade-out has played
static volatile bool guiExiting = false;

// A background thread can be parked waiting for the GUI thread to acknowledge
// an overlay (CancelAction) or answer a prompt (WindowPromptRequest)
static void ReleaseBackgroundWaiters()
{
	ProgressSync().mutex.lock();
	ProgressSync().idleCond.signal();
	ProgressSync().mutex.unlock();

	PromptSync().mutex.lock();
	PromptSync().idleCond.signal();
	PromptSync().mutex.unlock();
}

static bool UpdateGui()
{
	if(guiExiting)
		return false;

	ProcessInputData();

	menu->progressOverlayState.update();
	ServicePendingWindowPromptRequest();
	ServicePendingCreditsWindowRequest();

	DrawGui();

	if(platform->shouldExit())
	{
		for(int a = 0; a <= 255; a += 15)
		{
			menu->mainWindow.draw();
			platform->getVideo()->getImageRenderer()->drawRectangle(0,0,platform->getVideo()->getScreenWidth(),platform->getVideo()->getScreenHeight(),(PixelColor){0, 0, 0, (uint8_t)a});
			platform->getVideo()->renderMenu();
		}
		guiExiting = true;
		ReleaseBackgroundWaiters();
		return false;
	}

	return true;
}

/****************************************************************************
 * WindowPrompt
 *
 * Displays a prompt window to user, with information, an error message, or
 * presenting a user with a choice.
 ***************************************************************************/
static int WindowPrompt(const char *title, const char *msg, const char *btn1Label, const char *btn2Label)
{
	if(!menu || platform->getStatus() != Status::Running) // no UI without the foreground
		return 0;

	int choice = -1;

	GuiWindow promptWindow(448,288);
	promptWindow.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	promptWindow.setPosition(0, -10);

	GuiTrigger trigB;
	trigB.setSecondaryTrigger();

	MenuDialogShape dialogBoxImg(MENU_DIALOG_W, MENU_DIALOG_H, MENU_DIALOG_HEADER_H);

	GuiText titleTxt(title, 26, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	titleTxt.setPosition(0, MENU_DIALOG_TITLE_Y);
	GuiText msgTxt(msg, 26, (PixelColor){0, 0, 0, 255});
	msgTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	msgTxt.setPosition(0,-20);
	msgTxt.setWrap(true, 430);

	GuiText btn1Txt(btn1Label, 22, (PixelColor){0, 0, 0, 255});
	MenuButtonShape btn1Img(MenuButtonShape::NORMAL);
	GuiButton btn1(MENU_BTN_NORMAL_W, MENU_BTN_NORMAL_H);

	if(btn2Label)
	{
		btn1.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
		btn1.setPosition(20, -25);
	}
	else
	{
		btn1.setAlignment(ALIGN_H::CENTRE, ALIGN_V::BOTTOM);
		btn1.setPosition(0, -25);
		btn1.setTrigger(&trigB);
	}

	btn1.setLabel(&btn1Txt);
	btn1.setImage(&btn1Img);
	btn1.setTrigger(trigA);
	btn1.setState(STATE::SELECTED);
	btn1.setEffectGrow();

	MenuButton btn2(MenuButtonShape::NORMAL, btn2Label, ALIGN_H::RIGHT, ALIGN_V::BOTTOM, -20, -25);

	promptWindow.append(&dialogBoxImg);
	promptWindow.append(&titleTxt);
	promptWindow.append(&msgTxt);
	promptWindow.append(&btn1);

	if(btn2Label)
	{
		promptWindow.append(&btn2);
		btn2.setTrigger(&trigB);
	}

	promptWindow.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_IN, 50);
	CancelAction();
	menu->mainWindow.setState(STATE::DISABLED);
	menu->mainWindow.appendWithAutoRemove(&promptWindow);
	menu->mainWindow.changeFocus(&promptWindow);
	if(btn2Label)
	{
		btn2.resetState();
		btn1.setState(STATE::SELECTED);
	}

	while(choice == -1)
	{
		if(!UpdateGui()) return 0;

		if(btn1.getState() == STATE::CLICKED)
			choice = 1;
		else if(btn2.getState() == STATE::CLICKED)
			choice = 0;
	}

	promptWindow.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_OUT, 50);
	while(promptWindow.getEffect() > 0)
	{
		if(!UpdateGui()) return choice;
	}
	menu->mainWindow.setState(STATE::DEFAULT);
	return choice;
}

/****************************************************************************
 * CancelAction
 *
 * Cancels any progress/action overlay currently showing (or about to show),
 * and waits for it to be fully torn down before returning.
 ***************************************************************************/
void CancelAction()
{
	if(!menu) return;

	ProgressSync().mutex.lock();
	showProgress = 0;
	progressCancelFn = nullptr;
	ProgressSync().mutex.unlock();

	if(IsMainThread())
	{
		menu->progressOverlayState.update(); // synchronously reflect showProgress==0 right now
	}
	else
	{
		ProgressSync().mutex.lock();

		while(!progIdle && !guiExiting) // nobody acknowledges once the GUI thread has stopped
			ProgressSync().idleCond.wait(ProgressSync().mutex);

		ProgressSync().mutex.unlock();
	}
}

/****************************************************************************
 * ShowProgress
 *
 * Updates the variables used by the progress window for drawing a progress
 * bar. Safe to call from a background worker thread - see CancelAction().
 ***************************************************************************/
void ShowProgress (const char *msg, int done, int total)
{
	if(!menu)
		return;

	if(total < (256*1024))
		return;
	else if(done > total) // this shouldn't happen
		done = total;

	if(done/total > 0.99)
		done = total;

	if(showProgress != 1)
		CancelAction(); // wait for previous progress window to finish

	ProgressSync().mutex.lock();
	snprintf(progressMsg, 200, "%s", msg);
	sprintf(progressTitle, "Please Wait");
	showProgress = 1;
	progressCancelFn = nullptr;
	progressTotal = total;
	progressDone = done;
	progIdle = false;
	ProgressSync().mutex.unlock();
}

/****************************************************************************
 * ShowAction
 *
 * Shows that an action is underway. Safe to call from a background worker
 * thread - see CancelAction().
 ***************************************************************************/
void ShowAction (const char *msg, void (*onCancel)(void))
{
	if(!menu)
		return;

	if(showProgress != 0)
		CancelAction(); // wait for previous progress window to finish

	ProgressSync().mutex.lock();
	snprintf(progressMsg, 200, "%s", msg);
	sprintf(progressTitle, "Please Wait");
	showProgress = 2;
	progressCancelFn = onCancel;
	progressDone = 0;
	progressTotal = 0;
	progIdle = false;
	ProgressSync().mutex.unlock();
}

static int WindowPromptRequest(const char *title, const char *msg, const char *btn1Label, const char *btn2Label)
{
	if(IsMainThread())
		return WindowPrompt(title, msg, btn1Label, btn2Label);

	if(!menu || guiExiting || platform->getStatus() != Status::Running)
		return 0;

	PromptSync().mutex.lock();
	promptPendingTitle = title;
	promptPendingMsg = msg;
	promptPendingBtn1 = btn1Label;
	promptPendingBtn2 = btn2Label;
	promptResultReady = false;
	promptPending = true;
	while(!promptResultReady && !guiExiting) // nobody answers once the GUI thread has stopped
		PromptSync().idleCond.wait(PromptSync().mutex);
	int result = promptResultReady ? promptResult : 0;
	promptPending = false;
	PromptSync().mutex.unlock();
	return result;
}

void ErrorPrompt(const char *msg)
{
	WindowPromptRequest("Error", msg, "OK", nullptr);
}

int ErrorPromptRetry(const char *msg)
{
	return WindowPromptRequest("Error", msg, "Retry", "Cancel");
}

void InfoPrompt(const char *msg)
{
	WindowPromptRequest("Information", msg, "OK", nullptr);
}

/****************************************************************************
 * OnScreenKeyboard
 *
 * Opens an on-screen keyboard window, with the data entered being stored
 * into the specified variable.
 ***************************************************************************/
static void OnScreenKeyboard(char * var, uint32_t maxlen)
{
	int save = -1;

	GuiKeyboard keyboard(var, maxlen);

	GuiText okBtnTxt("OK", 22, (PixelColor){0, 0, 0, 255});
	MenuButtonShape okBtnImg(MenuButtonShape::NORMAL);
	GuiButton okBtn(MENU_BTN_NORMAL_W, MENU_BTN_NORMAL_H);

	okBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
	okBtn.setPosition(25, -25);

	okBtn.setLabel(&okBtnTxt);
	okBtn.setImage(&okBtnImg);
	okBtn.setTrigger(trigA);
	okBtn.setEffectGrow();

	MenuButton cancelBtn(MenuButtonShape::NORMAL, "Cancel", ALIGN_H::RIGHT, ALIGN_V::BOTTOM, -25, -25);

	MenuKeyboardBox keyboardBox;
	keyboard.insert(&keyboardBox, 0);
	keyboard.append(&okBtn);
	keyboard.append(&cancelBtn);

	menu->mainWindow.setState(STATE::DISABLED);
	menu->mainWindow.appendWithAutoRemove(&keyboard);
	menu->mainWindow.changeFocus(&keyboard);

	while(save == -1)
	{
		if(!UpdateGui()) return;

		if(okBtn.getState() == STATE::CLICKED)
			save = 1;
		else if(cancelBtn.getState() == STATE::CLICKED)
			save = 0;
	}

	if(save)
	{
		snprintf(var, maxlen, "%s", keyboard.kbtextstr);
	}

	menu->mainWindow.setState(STATE::DEFAULT);
}

/****************************************************************************
 * SettingWindow
 *
 * Opens a new window, with the specified window element appended. Allows
 * for a customizable prompted setting.
 ***************************************************************************/
static int
SettingWindow(const char * title, GuiWindow * w)
{
	int save = -1;

	GuiWindow promptWindow(448,288);
	promptWindow.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);

	MenuDialogShape dialogBoxImg(MENU_DIALOG_W, MENU_DIALOG_H, MENU_DIALOG_HEADER_H);

	GuiText titleTxt(title, 26, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	titleTxt.setPosition(0, MENU_DIALOG_TITLE_Y);

	GuiText okBtnTxt("OK", 22, (PixelColor){0, 0, 0, 255});
	MenuButtonShape okBtnImg(MenuButtonShape::NORMAL);
	GuiButton okBtn(MENU_BTN_NORMAL_W, MENU_BTN_NORMAL_H);

	okBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
	okBtn.setPosition(20, -25);

	okBtn.setLabel(&okBtnTxt);
	okBtn.setImage(&okBtnImg);
	okBtn.setTrigger(trigA);
	okBtn.setEffectGrow();

	MenuButton cancelBtn(MenuButtonShape::NORMAL, "Cancel", ALIGN_H::RIGHT, ALIGN_V::BOTTOM, -20, -25);

	promptWindow.append(&dialogBoxImg);
	promptWindow.append(&titleTxt);
	promptWindow.append(&okBtn);
	promptWindow.append(&cancelBtn);

	menu->mainWindow.setState(STATE::DISABLED);
	menu->mainWindow.appendWithAutoRemove(&promptWindow);
	menu->mainWindow.appendWithAutoRemove(w);
	menu->mainWindow.changeFocus(w);

	while(save == -1)
	{
		if(!UpdateGui()) return 0;

		if(okBtn.getState() == STATE::CLICKED)
			save = 1;
		else if(cancelBtn.getState() == STATE::CLICKED)
			save = 0;
	}
	menu->mainWindow.setState(STATE::DEFAULT);
	return save;
}

/****************************************************************************
 * MenuGameSelection
 *
 * Displays a list of games on the specified load device, and allows the user
 * to browse and select from this list.
 ***************************************************************************/
static char* getImageFolder()
{
	switch(EmuSettings.previewImage)
	{
		case PREVIEWIMAGE_SCREENSHOT : return EmuSettings.screenshotsFolder;
		case PREVIEWIMAGE_COVER : return EmuSettings.coverFolder;
		case PREVIEWIMAGE_ARTWORK : return EmuSettings.artworkFolder;
		default : return EmuSettings.coverFolder;
	}
}

static bool ResolvePreviewImagePath(void *, int index, char * outPath, size_t outPathSize)
{
	if(browser.dir[0] == 0 || EmuSettings.loadDevice <= 0 ||
	   browser.numEntries <= 0 || index <= 0 || index >= browser.numEntries)
		return false;

	char imageBase[MAXJOLIET + 1];
	if(browserList[index].isdir)
		snprintf(imageBase, sizeof(imageBase), "%s", browserList[index].filename);
	else
		StripExt(imageBase, browserList[index].filename);

	char imageFile[MAXJOLIET + 16];
	snprintf(imageFile, sizeof(imageFile), "%s.png", imageBase);
	// no image to show when the path to it does not fit
	return platform->getFileSystem()->getPath(outPath, outPathSize, EmuSettings.loadDevice, getImageFolder(), imageFile);
}

// Fits the preview image inside its card (with a margin) and sizes the shadow behind it to match
static void OnPreviewImageChanged(void * context, GuiImage * target, bool isDefault)
{
	MenuCoverShadow * shadow = (MenuCoverShadow *)context;
	int w = 0, h = 0;

	if(target->getWidth() > 0 && target->getHeight() > 0)
	{
		const float scale = std::min((MENU_PREVIEW_W - 36.0f) / target->getWidth(), (MENU_PREVIEW_H - 30.0f) / target->getHeight());
		target->setScale(scale);
		w = (int)(target->getWidth() * scale + 0.5f);
		h = (int)(target->getHeight() * scale + 0.5f);
	}
	shadow->fit(isDefault ? 0 : w, isDefault ? 0 : h); // the default image has no drop shadow
}

static int BrowserLoadFileTask(void *) { return BrowserLoadFile(); }
static int BrowserChangeFolderTask(void *) { return BrowserChangeFolder(); }

struct ChangeInterfaceArgs
{
	int device;
	bool silent;
};

static int ChangeInterfaceTask(void * arg) {
	ChangeInterfaceArgs * a = (ChangeInterfaceArgs *)arg;
	return ChangeInterface(a->device, a->silent) ? 1 : 0;
}

static int OpenGameListTask(void *) { return OpenGameList(); }

static int AutoDetectDevicesTask(void *) { AutoDetectDevices(); return 0; }

/****************************************************************************
 * RunWithGuiUpdates
 * Runs a task on the worker thread while the GUI keeps drawing (and the main
 * window ignores input).
 * If the app is asked to quit while waiting the task may still be running, so
 * anything it uses through arg must outlive the caller's stack frame (static
 * or heap storage).
 * \return false if the app was asked to quit while waiting.
 ***************************************************************************/
static bool RunWithGuiUpdates(BgTaskFn task, int * result = nullptr, void * arg = nullptr)
{
	GuiInputBlock block;

	int r;
	if(RunOnWorkerThread(task, arg))
	{
		while(!IsWorkerThreadFinished())
		{
			if(!UpdateGui()) return false;
		}
		r = GetWorkerThreadResult();
	}
	else
	{
		r = task(arg);
	}

	if(result)
		*result = r;

	return true;
}

/****************************************************************************
 * WaitForQueuedTasks
 * Lets queued background work (eg. the auto-save of the game that was just
 * left) finish before something that reads the files it writes. The GUI keeps
 * drawing meanwhile, and it returns straight away when nothing is queued.
 * \return false if the app was asked to quit while waiting.
 ***************************************************************************/
static bool WaitForQueuedTasks()
{
	GuiInputBlock block;

	while(!BackgroundTasksIdle())
	{
		if(!UpdateGui()) return false;
	}
	return true;
}

/****************************************************************************
 * AutoSaveLeavingGame
 *
 * What the Save Mode saves when the game is replaced by another: the
 * Snapshot, Cartridge Save, or nothing. (Quitting is AutoSaveOnQuit's.)
 *
 * Returning from the game to the menu saves nothing: whoever dies and goes
 * to the menu to load the Auto Snapshot wants the moment they started from,
 * not the one they died at. The game is only left for good when another is
 * chosen, or the app quits - or when the Saves screen is asked to update the
 * autosave.
 *
 * Nothing is asked: a Snapshot goes to the game's auto slot (slot 0), never to
 * a numbered one, so it can't overwrite anything the user saved by hand. And
 * nothing is written for a game that has not changed since it was last saved.
 *
 * Everything that has to happen while the game is still loaded - capturing
 * the Cartridge Save or Snapshot - is done right here on the main thread. Writing it to the
 * storage device is then queued as a silent background task, so the menu
 * never waits on the device and nothing on screen is disabled or covered
 * while it happens.
 * \return false if the app was asked to quit
 ***************************************************************************/
static bool AutoSaveLeavingGame()
{
	// no game loaded, nothing that saves on its own, or nothing changed since it was saved
	if(!N64IsLoaded() || EmuSettings.saveMode == SAVEMODE_MANUAL || !LiveStateChanged())
		return true;

	// the core is not thread safe: this runs here, on the thread that owns it. The
	// game's own saves are written first, they are small and matter the most.
	AutoSaveOnQuit(ScreenshotIsCurrent());
	return true;
}

static int MenuGameSelection()
{
	int selection = MENU_NONE;
	bool res;
	int i;

	GuiText titleTxt("Choose Game", 26, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.setPosition(50,50);

	GuiImageData iconHome(icon_home_png);
	GuiImageData iconSettings(icon_settings_png);

	GuiTrigger trigHome;
	trigHome.setButtonOnlyTrigger(-1, INPUT_BTN_HOME);

	GuiText settingsBtnTxt("Settings", 22, (PixelColor){0, 0, 0, 255});
	GuiImage settingsBtnIcon(&iconSettings);
	settingsBtnIcon.setAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
	settingsBtnIcon.setPosition(14,0);
	MenuButtonShape settingsBtnImg(MenuButtonShape::LONG);
	GuiButton settingsBtn(MENU_BTN_LONG_W, MENU_BTN_LONG_H);
	settingsBtn.setAlignment(ALIGN_H::LEFT, ALIGN_V::BOTTOM);
	settingsBtn.setPosition(90, -35);
	settingsBtn.setLabel(&settingsBtnTxt);
	settingsBtn.setIcon(&settingsBtnIcon);
	settingsBtn.setImage(&settingsBtnImg);
	settingsBtn.setTrigger(trigA);
	settingsBtn.setEffectGrow();

	GuiText exitBtnTxt("Exit", 22, (PixelColor){0, 0, 0, 255});
	GuiImage exitBtnIcon(&iconHome);
	exitBtnIcon.setAlignment(ALIGN_H::LEFT, ALIGN_V::MIDDLE);
	exitBtnIcon.setPosition(14,0);
	MenuButtonShape exitBtnImg(MenuButtonShape::LONG);
	GuiButton exitBtn(MENU_BTN_LONG_W, MENU_BTN_LONG_H);
	exitBtn.setAlignment(ALIGN_H::RIGHT, ALIGN_V::BOTTOM);
	exitBtn.setPosition(-90, -35);
	exitBtn.setLabel(&exitBtnTxt);
	exitBtn.setIcon(&exitBtnIcon);
	exitBtn.setImage(&exitBtnImg);
	exitBtn.setTrigger(trigA);

	#if defined(HW_RVL) || defined(HW_DOL)
	exitBtn.setTrigger(&trigHome);
	#endif

	exitBtn.setEffectGrow();

	GuiWindow buttonWindow(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());
	buttonWindow.append(&settingsBtn);
	buttonWindow.append(&exitBtn);

	#ifdef __WIIU__
	GuiButton hiddenHomeBtn;
	hiddenHomeBtn.setSoundOver(nullptr);
	hiddenHomeBtn.setSoundClick(nullptr);
	hiddenHomeBtn.setTrigger(&trigHome);
	hiddenHomeBtn.setSelectable(false);
	buttonWindow.append(&hiddenHomeBtn);
	#endif

	DeviceCheckingScope deviceChecking;

	GuiGameBrowser gameBrowser(MENU_LIST_W, MENU_LIST_H);
	gameBrowser.setPosition(MENU_LIST_X, MENU_LIST_Y);
	ResetBrowser();

	MenuCoverShadow previewShadow;
	previewShadow.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	previewShadow.setPosition(MENU_PREVIEW_X + MENU_PREVIEW_W/2 - platform->getVideo()->getScreenWidth()/2,
		MENU_LIST_Y + MENU_PREVIEW_H/2 - platform->getVideo()->getScreenHeight()/2);

	GuiImage preview;
	preview.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	preview.setPosition(MENU_PREVIEW_X + MENU_PREVIEW_W/2 - platform->getVideo()->getScreenWidth()/2,
		MENU_LIST_Y + MENU_PREVIEW_H/2 - platform->getVideo()->getScreenHeight()/2);

#if defined(HW_RVL) || defined(HW_DOL)
	gameBrowser.setPreviewImage(&preview, ResolvePreviewImagePath, nullptr, 1, 0, 640, 480, OnPreviewImageChanged, &previewShadow);
#else
	gameBrowser.setPreviewImage(&preview, ResolvePreviewImagePath, nullptr, 6, 2, 640, 480, OnPreviewImageChanged, &previewShadow);
#endif

	menu->btnLogo.setAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
	menu->btnLogo.setPosition(-50, 24);
	menu->mainWindow.appendWithAutoRemove(&titleTxt);
	menu->mainWindow.appendWithAutoRemove(&gameBrowser);
	menu->mainWindow.appendWithAutoRemove(&previewShadow);
	menu->mainWindow.appendWithAutoRemove(&preview);
	menu->mainWindow.appendWithAutoRemove(&buttonWindow);

	// populate initial directory listing
	selectLoadedFile = 1;
	if(!RunWithGuiUpdates(OpenGameListTask)) return MENU_EXIT;

	gameBrowser.resetState();
	gameBrowser.fileList[0]->setState(STATE::SELECTED);
	gameBrowser.triggerUpdate();
	if(inSz)
		titleTxt.setRawText(szname);
	else
		titleTxt.setText("Choose Game");

	while(selection == MENU_NONE)
	{
		if(!UpdateGui()) return MENU_EXIT;

		deviceChecking.update(); // starts device checking once its delay has elapsed

		// A device the currently-open folder lives on just disappeared - the cached browserList is stale
		const uint32_t removedMask = __atomic_exchange_n(&removedDeviceMask, 0, __ATOMIC_RELAXED);
		if(removedMask != 0)
		{
			if(browser.dir[0] != 0)
			{
				int currentDevice;
				if(!FindDevice(browser.dir, &currentDevice) || (removedMask & (1u << currentDevice)))
				{
					browser.dir[0] = 0;
					browserDeviceListChanged = true;
				}
			}
		}

		// A device appeared/disappeared since the last check  - refresh the device listing
		if(browserDeviceListChanged)
		{
			browserDeviceListChanged = false;

			if(browser.dir[0] == 0)
			{
				ResetBrowser();
				browser.numEntries = AddDeviceListing();
				gameBrowser.resetState();
				if(browser.numEntries > 0)
					gameBrowser.fileList[0]->setState(STATE::SELECTED);
				gameBrowser.triggerUpdate();
			}
		}

		if(selectLoadedFile == 2)
		{
			selectLoadedFile = 0;
			menu->mainWindow.changeFocus(&gameBrowser);
			gameBrowser.triggerUpdate();
		}

		// update gameWindow based on arrow buttons
		// set MENU_EXIT if A button pressed on a game
		for(i=0; i < FILE_PAGESIZE; i++)
		{
			if(gameBrowser.fileList[i]->getState() == STATE::CLICKED)
			{
				gameBrowser.fileList[i]->resetState();
				
				// check corresponding browser entry
				if(browserList[browser.selIndex].isdir || IsSz())
				{
					gameBrowser.setState(STATE::DISABLED);

					if(!RunOnWorkerThread(BrowserChangeFolderTask))
					{
						gameBrowser.setState(STATE::DEFAULT);
						continue;
					}

					while(!IsWorkerThreadFinished())
					{
						if(!UpdateGui()) return MENU_EXIT;
						gameBrowser.setState(STATE::DISABLED);
					}

					res = GetWorkerThreadResult() != 0;

					if(res)
					{
						gameBrowser.resetState();
						gameBrowser.fileList[0]->setState(STATE::SELECTED);
						gameBrowser.triggerUpdate();
					}
					else
					{
						selection = MENU_GAMESELECTION;
						break;
					}

					if(inSz)
						titleTxt.setRawText(szname);
					else
						titleTxt.setText("Choose Game");
				}
				else
				{
					menu->mainWindow.setState(STATE::DISABLED);

					// the game being replaced is left for good: its autosave is written first
					deviceChecking.beginLeave(); // the device thread winds down while the auto-save below runs
					if(!AutoSaveLeavingGame()) return MENU_EXIT;
					if(!WaitForQueuedTasks()) return MENU_EXIT; // for that write, and any other

					deviceChecking.leave();

					if(RunOnWorkerThread(BrowserLoadFileTask))
					{
						while(!IsWorkerThreadFinished())
						{
							if(!UpdateGui()) return MENU_EXIT;
						}

						if(GetWorkerThreadResult())
							selection = MENU_EXIT;
						else
							menu->mainWindow.setState(STATE::DEFAULT);
					}
					else
					{
						menu->mainWindow.setState(STATE::DEFAULT);
					}
				}
			}
		}

		if(settingsBtn.getState() == STATE::CLICKED) {
			selection = MENU_SETTINGS;
		}
		else if(exitBtn.getState() == STATE::CLICKED) {
			platform->getInput()->haltRumble();
			deviceChecking.beginLeave();
			platform->triggerExit();
		}

		#ifdef __WIIU__
		if(hiddenHomeBtn.getState() == STATE::CLICKED) {
			hiddenHomeBtn.resetState();
			static_cast<WutInputDriver*>(platform->getInput())->openHomeButtonOverlay();
		}
		#endif
	}

	HaltParseThread(); // halt parsing
	ResetBrowser();
	return selection;
}

#ifndef HW_DOL
static int playerMappingChan = 0;

static void PlayerMappingWindowUpdate(void * ptr, int dir)
{
	GuiButton * b = (GuiButton *)ptr;
	if(b->getState() == STATE::CLICKED)
	{
		playerMapping[playerMappingChan] += dir;

		if(playerMapping[playerMappingChan] > 3)
			playerMapping[playerMappingChan] = 0;
		if(playerMapping[playerMappingChan] < 0)
			playerMapping[playerMappingChan] = 3;

		settingText->setFormat("Player %d", playerMapping[playerMappingChan]+1);
		b->resetState();
	}
}

static void PlayerMappingWindowLeftClick(void * ptr) { PlayerMappingWindowUpdate(ptr, -1); }
static void PlayerMappingWindowRightClick(void * ptr) { PlayerMappingWindowUpdate(ptr, +1); }

static void PlayerMappingWindow(int chan)
{
	playerMappingChan = chan;

	GuiWindow * w = new GuiWindow(300,250);
	w->setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);

	MenuArrowButton arrowLeftBtn(SHAPE::TRIANGLE_LEFT, ALIGN_H::LEFT, ALIGN_V::MIDDLE, 0, 0, PlayerMappingWindowLeftClick);

	MenuArrowButton arrowRightBtn(SHAPE::TRIANGLE_RIGHT, ALIGN_H::RIGHT, ALIGN_V::MIDDLE, 0, 0, PlayerMappingWindowRightClick);
	
	settingText = new GuiText(nullptr, 22, (PixelColor){0, 0, 0, 255});
	settingText->setFormat("Player %d", playerMapping[playerMappingChan]+1);

	w->append(&arrowLeftBtn);
	w->append(&arrowRightBtn);
	w->append(settingText);

	const std::string title = TrFormat("Player Mapping - Controller %d", chan+1);

	int previousPlayerMapping = playerMapping[playerMappingChan];

	if(!SettingWindow(title.c_str(),w))
		playerMapping[playerMappingChan] = previousPlayerMapping; // undo changes

	delete(w);
	delete(settingText);
}
#endif

/****************************************************************************
 * FlushEmulatorAudio
 *
 * Whatever is queued for the speakers belongs to the game as it was. After a
 * reset or a loaded snapshot it must not play ahead of the new state, so the
 * queue is emptied (as when loading a new game). Only for use while in the
 * menu, when emulator audio is stopped.
 ***************************************************************************/
static void FlushEmulatorAudio()
{
	EmulatorAudioDriver * audio = platform->getAudio()->getEmulatorAudio();
	if(audio)
		audio->resetAudio();
}

/****************************************************************************
 * SelectButton
 *
 * Puts the highlight on one button of a page. A page highlights its top-most
 * button when it first gets focus (usually Close), and a prompt on top of it
 * clears the highlight, so a page that wants a particular button highlighted
 * - when it opens, or after a prompt is cancelled - does it with this.
 ***************************************************************************/
static void SelectButton(GuiWindow & page, GuiElement & button)
{
	menu->mainWindow.changeFocus(&page); // lets the page pick its default first...
	page.resetState();                   // ...which is then replaced
	button.setState(STATE::SELECTED);
}

/****************************************************************************
 * MenuGame
 *
 * Menu displayed when returning to the menu from in-game.
 ***************************************************************************/
static int MenuGame()
{
	int selection = MENU_NONE;

	if(lastMenu == MENU_NONE) // coming from the game
	{
		enterSound->play();
		menu->btnLogo.setAlignment(ALIGN_H::RIGHT, ALIGN_V::BOTTOM);
		menu->btnLogo.setPosition(-50, -40);
		menu->bgTopImg.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_IN, 35);
		menu->bgBottomImg.setEffect(EFFECT::SLIDE_BOTTOM | EFFECT::SLIDE_IN, 35);
		menu->btnLogo.setEffect(EFFECT::SLIDE_BOTTOM | EFFECT::SLIDE_IN, 35);
	}

	char romTitle[512];
	platform->getFileSystem()->nameToDisplay(n64RomName, romTitle, sizeof(romTitle)); // raw name -> UTF-8 for display only
	GuiText titleTxt(nullptr, 22, (PixelColor){255, 255, 255, 255});
	titleTxt.setRawText(romTitle);
	titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.setPosition(50,40);

	MenuIconData iconMappings(icon_settings_mappings_png, -4.0f);
	MenuIconData iconSave(icon_game_save_png);
	MenuIconData iconReset(icon_game_reset_png);

	GuiTrigger trigHome;
	GuiTrigger trigB;
	trigHome.setButtonOnlyTrigger(-1, INPUT_BTN_HOME);
	trigB.setSecondaryTrigger();
	
	MenuLargeButton mappingBtn("Button Mappings", &iconMappings, -200, 150, 30);

	MenuLargeButton saveBtn("Saves", &iconSave, 0, 150);

	MenuLargeButton resetBtn("Reset", &iconReset, 200, 150);

	// Second row: Game Settings
	MenuButton gameSettingsBtn(MenuButtonShape::NORMAL, "Game Settings", ALIGN_H::CENTRE, ALIGN_V::TOP, 0, 290);

	MenuButton mainmenuBtn(MenuButtonShape::NORMAL, EmuSettings.autoloadGame ? "Exit" : "Main Menu", ALIGN_H::CENTRE, ALIGN_V::BOTTOM, 0, -35);

	MenuButton closeBtn(MenuButtonShape::SMALL, "Close", ALIGN_H::RIGHT, ALIGN_V::TOP, -50, 35, &trigHome, 20);
	closeBtn.setTrigger(&trigB);
	closeBtn.setSoundClick(nullptr);

	#ifndef HW_DOL
	ControllerStatus controllerStatus(trigA);
	int controllerClicked;
	#endif

	GuiWindow w(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());
	w.append(&titleTxt);
	w.append(&saveBtn);
	w.append(&mappingBtn);
	w.append(&resetBtn);
	w.append(&gameSettingsBtn);

	#ifndef HW_DOL
	controllerStatus.appendTo(w);
	#endif

	w.append(&mainmenuBtn);
	w.append(&closeBtn);

	menu->btnLogo.setAlignment(ALIGN_H::RIGHT, ALIGN_V::BOTTOM);
	menu->btnLogo.setPosition(-50, -40);
	menu->mainWindow.appendWithAutoRemove(&w);

	// Opening from the game: Saves, the usual reason to open this menu. Coming back from one of its pages:
	// the button that opened it.
	switch(previousMenu)
	{
		case MENU_GAMESETTINGS_MAPPINGS:
			SelectButton(w, mappingBtn);
			break;
		case MENU_GAMESETTINGS:
			SelectButton(w, gameSettingsBtn);
			break;
		default:
			SelectButton(w, saveBtn);
			break;
	}

	if(lastMenu == MENU_NONE)
	{
		closeBtn.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_IN, 35);
		titleTxt.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_IN, 35);
		mainmenuBtn.setEffect(EFFECT::SLIDE_BOTTOM | EFFECT::SLIDE_IN, 35);
		#ifndef HW_DOL
		controllerStatus.setEffect(EFFECT::SLIDE_BOTTOM | EFFECT::SLIDE_IN, 35);
		#endif

		w.setEffect(EFFECT::FADE, 15);
	}

	while(selection == MENU_NONE)
	{
		if(!UpdateGui()) return MENU_EXIT;

		#ifndef HW_DOL
		controllerStatus.update();
		#endif

		if(saveBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_GAME_SAVE;
		}
		else if(mappingBtn.getState() == STATE::CLICKED)
		{
			MappingScreenOpenedBy(mappingBtn.getStateChan());
			selection = MENU_GAMESETTINGS_MAPPINGS;
		}
		else if(resetBtn.getState() == STATE::CLICKED)
		{
			if (WindowPrompt("Reset Game", "Are you sure that you want to reset this game? Any unsaved progress will be lost.", "OK", "Cancel"))
			{
				N64Reset();
				MarkLiveStateChanged();
				FlushEmulatorAudio();
				selection = MENU_EXIT;
			}
			else
			{
				SelectButton(w, resetBtn);
			}
		}
		else if(gameSettingsBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_GAMESETTINGS;
		}
#ifndef HW_DOL
		else if((controllerClicked = controllerStatus.clicked()) >= 0)
		{
			PlayerMappingWindow(controllerClicked);
		}
#endif
		else if(mainmenuBtn.getState() == STATE::CLICKED)
		{
			if (WindowPrompt("Quit Game", "Quit this game? Any unsaved progress will be lost.", "OK", "Cancel"))
			{
				// The game is left for good: its autosave is written now, with its screenshot - which is
				// cleared just below, and would be gone by the time another game is chosen
				if(!AutoSaveLeavingGame()) return MENU_EXIT;

				menu->mainWindow.remove(gameScreenImg);
				delete gameScreenImg;
				if(gameScreenTexture != nullptr) {
					platform->getVideo()->getImageRenderer()->destroyTexture(gameScreenTexture);
					gameScreenTexture = nullptr;
				}
				ClearScreenshot();
				if(EmuSettings.autoloadGame) {
					ExitApp();
				}
				else {
					gameScreenImg = new MenuBackgroundShape();
					menu->mainWindow.insert(gameScreenImg, 0);
					bgMusic->play();
					selection = MENU_GAMESELECTION;
				}
			}
			else
			{
				SelectButton(w, mainmenuBtn);
			}
		}
		else if(closeBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_EXIT;

			exitSound->play();
			menu->bgTopImg.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_OUT, 15);
			closeBtn.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_OUT, 15);
			titleTxt.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_OUT, 15);
			mainmenuBtn.setEffect(EFFECT::SLIDE_BOTTOM | EFFECT::SLIDE_OUT, 15);
			menu->bgBottomImg.setEffect(EFFECT::SLIDE_BOTTOM | EFFECT::SLIDE_OUT, 15);
			menu->btnLogo.setEffect(EFFECT::SLIDE_BOTTOM | EFFECT::SLIDE_OUT, 15);
			#ifndef HW_DOL
			controllerStatus.setEffect(EFFECT::SLIDE_BOTTOM | EFFECT::SLIDE_OUT, 15);
			#endif

			w.setEffect(EFFECT::FADE, -15);

			while(w.getEffect() > 0)
			{
				if(!UpdateGui()) return MENU_EXIT;
			}
		}
	}


	return selection;
}

/****************************************************************************
 * MenuGameSaves
 *
 * The ten save state slots of the game. Slot 0 is the Auto Snapshot that the
 * Save Mode keeps for you. The first row switches between saving and loading.
 ***************************************************************************/
#define SAVE_SLOTS 10

static void SlotDescription(int slot, char * out, size_t outSize)
{
	char path[MAXPATHLEN];
	struct stat st;

	if(!N64StatePath(path, sizeof(path), slot) || stat(path, &st) != 0)
	{
		snprintf(out, outSize, "Empty");
		return;
	}

	struct tm * t = localtime(&st.st_mtime);
	if(t)
		strftime(out, outSize, "%Y-%m-%d %H:%M", t);
	else
		snprintf(out, outSize, "Saved");
}

static int MenuGameSaves()
{
	int selection = MENU_NONE;
	int ret;
	bool loadMode = (lastMenu == MENU_GAME_LOAD);
	bool firstRun = true;
	OptionList options;

	snprintf(options.name[0], sizeof(options.name[0]), "Action");
	for(int slot = 0; slot < SAVE_SLOTS; slot++)
	{
		if(slot == 0)
			snprintf(options.name[1], sizeof(options.name[1]), "Slot 0 (Auto Snapshot)");
		else
			snprintf(options.name[slot + 1], sizeof(options.name[slot + 1]), "Slot %d", slot);
	}
	options.length = SAVE_SLOTS + 1;

	for(int i = 0; i < options.length; i++)
		options.value[i][0] = 0;

	GuiText titleTxt("Saves", 26, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.setPosition(50,50);

	GuiTrigger trigB;
	trigB.setSecondaryTrigger();

	MenuButton backBtn(MenuButtonShape::NORMAL, "Go Back", ALIGN_H::LEFT, ALIGN_V::BOTTOM, 50, -35, &trigB);

	GuiOptionBrowser optionBrowser(MENU_OPTIONS_W, MENU_OPTIONS_H, &options);
	optionBrowser.setPosition(0, MENU_LIST_Y);
	optionBrowser.setCol2Position(260);
	optionBrowser.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);

	GuiWindow w(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());
	w.append(&backBtn);
	menu->mainWindow.appendWithAutoRemove(&optionBrowser);
	menu->mainWindow.appendWithAutoRemove(&w);
	menu->mainWindow.appendWithAutoRemove(&titleTxt);

	while(selection == MENU_NONE)
	{
		if(!UpdateGui()) return MENU_EXIT;

		ret = optionBrowser.getClickedOption();

		if(ret == 0)
		{
			loadMode = !loadMode;
		}
		else if(ret > 0)
		{
			const int slot = ret - 1;
			char question[160];

			if(loadMode)
			{
				if(!N64StateExists(slot))
				{
					InfoPrompt("This slot is empty.");
				}
				else
				{
					snprintf(question, sizeof(question), "Load the state in slot %d? Any unsaved progress will be lost.", slot);
					if(WindowPrompt("Load State", question, "Load", "Cancel") && N64LoadState(slot, NOTSILENT))
					{
						MarkLiveStateChanged();
						FlushEmulatorAudio();
						selection = MENU_EXIT;
					}
				}
			}
			else
			{
				bool doSave = true;
				if(N64StateExists(slot))
				{
					snprintf(question, sizeof(question), "Overwrite the state in slot %d?", slot);
					doSave = WindowPrompt("Save State", question, "Save", "Cancel") != 0;
				}

				if(doSave && N64SaveState(slot, NOTSILENT))
				{
					if(slot == 0)
						AutoSnapshotWritten();
					InfoPrompt("State saved.");
				}
			}
		}

		if(ret >= 0 || firstRun)
		{
			firstRun = false;

			snprintf(options.value[0], sizeof(options.value[0]), "%s", loadMode ? "Load" : "Save");
			for(int slot = 0; slot < SAVE_SLOTS; slot++)
				SlotDescription(slot, options.value[slot + 1], sizeof(options.value[slot + 1]));

			optionBrowser.triggerUpdate();
		}

		if(backBtn.getState() == STATE::CLICKED)
			selection = MENU_GAME;
	}

	return selection;
}

/****************************************************************************
 * MenuGameSettings
 ***************************************************************************/
static int MenuGameSettings()
{
	int selection = MENU_NONE;
	char filepath[1024];

	GuiText titleTxt("Game Settings", 26, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.setPosition(50,50);

	MenuIconData iconVideo(icon_settings_video_png);
	MenuIconData iconEmulation(icon_settings_emulation_png);
	MenuIconData iconScreenshot(icon_settings_screenshot_png);

	GuiTrigger trigHome;
	GuiTrigger trigB;
	trigHome.setButtonOnlyTrigger(-1, INPUT_BTN_HOME);
	trigB.setSecondaryTrigger();

	MenuLargeButton emulationBtn("Emulation", &iconEmulation, -100, 120);

	MenuLargeButton videoBtn("Video", &iconVideo, 100, 120);

	MenuLargeButton screenshotBtn("Screenshot", &iconScreenshot, 0, 250);

	MenuButton closeBtn(MenuButtonShape::SMALL, "Close", ALIGN_H::RIGHT, ALIGN_V::TOP, -50, 35, &trigHome, 20);
	closeBtn.setSoundClick(nullptr);

	MenuButton backBtn(MenuButtonShape::NORMAL, "Go Back", ALIGN_H::LEFT, ALIGN_V::BOTTOM, 50, -35, &trigB);

	GuiWindow w(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());
	w.append(&titleTxt);
	w.append(&videoBtn);
	w.append(&emulationBtn);
	w.append(&screenshotBtn);
	w.append(&closeBtn);
	w.append(&backBtn);

	menu->mainWindow.appendWithAutoRemove(&w);

	// Opening from the game menu: the first button. Coming back from one of its pages: the button that opened it.
	switch(previousMenu)
	{
		case MENU_GAMESETTINGS_VIDEO:
			SelectButton(w, videoBtn);
			break;
		default:
			SelectButton(w, emulationBtn);
			break;
	}

	while(selection == MENU_NONE)
	{
		if(!UpdateGui()) return MENU_EXIT;

		if(videoBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_GAMESETTINGS_VIDEO;
		}
		else if(emulationBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_GAMESETTINGS_EMULATION;
		}
		else if(screenshotBtn.getState() == STATE::CLICKED)
		{
			if (WindowPrompt("Preview Screenshot", "Save a new Preview Screenshot? Current Screenshot image will be overwritten.", "OK", "Cancel"))
			{
				if(platform->getFileSystem()->getPath(filepath, EmuSettings.loadDevice, EmuSettings.screenshotsFolder, n64RomName))
				{
					if(gameScreenPng.size > 0)
					{
						strncat(filepath, ".png", sizeof(filepath) - strlen(filepath) - 1);
						SaveFile((char *)gameScreenPng.buffer, filepath, gameScreenPng.size, NOTSILENT);
					}
				}
				else
					ErrorPrompt("Maximum filepath length reached!");
			}
			SelectButton(w, screenshotBtn);
		}
		else if(closeBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_EXIT;

			exitSound->play();
			menu->bgTopImg.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_OUT, 15);
			closeBtn.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_OUT, 15);
			titleTxt.setEffect(EFFECT::SLIDE_TOP | EFFECT::SLIDE_OUT, 15);
			backBtn.setEffect(EFFECT::SLIDE_BOTTOM | EFFECT::SLIDE_OUT, 15);
			menu->bgBottomImg.setEffect(EFFECT::SLIDE_BOTTOM | EFFECT::SLIDE_OUT, 15);
			menu->btnLogo.setEffect(EFFECT::SLIDE_BOTTOM | EFFECT::SLIDE_OUT, 15);

			w.setEffect(EFFECT::FADE, -15);

			while(w.getEffect() > 0)
			{
				if(!UpdateGui()) return MENU_EXIT;
			}
		}
		else if(backBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_GAME;
		}
	}

	return selection;
}

GuiWindow& MenuMainWindow()
{
	return menu->mainWindow;
}

bool MenuUpdateGui()
{
	return UpdateGui();
}

bool MenuConfirm(const char * title, const char * msg, const char * yes, const char * no)
{
	return WindowPrompt(title, msg, yes, no) == 1;
}

/****************************************************************************
 * MenuSettingsVideo
 ***************************************************************************/

static void ScreenZoomWindowUpdate(void * ptr, float h, float v)
{
	GuiButton * b = (GuiButton *)ptr;
	if(b->getState() == STATE::CLICKED)
	{
		EmuSettings.videoZoomHor += h;
		EmuSettings.videoZoomVert += v;

		char zoom[10];
		sprintf(zoom, "%.2f%%", EmuSettings.videoZoomHor*100);
		settingText->setRawText(zoom);
		sprintf(zoom, "%.2f%%", EmuSettings.videoZoomVert*100);
		settingText2->setRawText(zoom);
		b->resetState();
	}
}

static void ScreenZoomWindowLeftClick(void * ptr) { ScreenZoomWindowUpdate(ptr, -0.01, 0); }
static void ScreenZoomWindowRightClick(void * ptr) { ScreenZoomWindowUpdate(ptr, +0.01, 0); }
static void ScreenZoomWindowUpClick(void * ptr) { ScreenZoomWindowUpdate(ptr, 0, +0.01); }
static void ScreenZoomWindowDownClick(void * ptr) { ScreenZoomWindowUpdate(ptr, 0, -0.01); }

static void ScreenZoomWindow()
{
	GuiWindow * w = new GuiWindow(200,200);
	w->setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);

	MenuArrowButton arrowLeftBtn(SHAPE::TRIANGLE_LEFT, ALIGN_H::CENTRE, ALIGN_V::MIDDLE, 50, 0, ScreenZoomWindowLeftClick);

	MenuArrowButton arrowRightBtn(SHAPE::TRIANGLE_RIGHT, ALIGN_H::CENTRE, ALIGN_V::MIDDLE, 164, 0, ScreenZoomWindowRightClick);

	MenuArrowButton arrowUpBtn(SHAPE::TRIANGLE_UP, ALIGN_H::CENTRE, ALIGN_V::MIDDLE, -76, -27, ScreenZoomWindowUpClick);

	MenuArrowButton arrowDownBtn(SHAPE::TRIANGLE_DOWN, ALIGN_H::CENTRE, ALIGN_V::MIDDLE, -76, 27, ScreenZoomWindowDownClick);

	MenuScreenShape screenPositionImg;
	screenPositionImg.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	screenPositionImg.setPosition(0, 0);

	settingText = new GuiText(nullptr, 20, (PixelColor){0, 0, 0, 255});
	settingText2 = new GuiText(nullptr, 20, (PixelColor){0, 0, 0, 255});
	char zoom[10];
	sprintf(zoom, "%.2f%%", EmuSettings.videoZoomHor*100);
	settingText->setRawText(zoom);
	settingText->setPosition(108, 0);
	sprintf(zoom, "%.2f%%", EmuSettings.videoZoomVert*100);
	settingText2->setRawText(zoom);
	settingText2->setPosition(-76, 0);

	float currentZoomHor = EmuSettings.videoZoomHor;
	float currentZoomVert = EmuSettings.videoZoomVert;

	w->append(&arrowLeftBtn);
	w->append(&arrowRightBtn);
	w->append(&arrowUpBtn);
	w->append(&arrowDownBtn);
	w->append(&screenPositionImg);
	w->append(settingText);
	w->append(settingText2);

	if(!SettingWindow("Screen Zoom",w))
	{
		// undo changes
		EmuSettings.videoZoomHor = currentZoomHor;
		EmuSettings.videoZoomVert = currentZoomVert;
	}

	delete(w);
	delete(settingText);
	delete(settingText2);
}

static void ScreenPositionWindowUpdate(void * ptr, int x, int y)
{
	GuiButton * b = (GuiButton *)ptr;
	if(b->getState() == STATE::CLICKED)
	{
		EmuSettings.videoXshift += x;
		EmuSettings.videoYshift += y;

		if(!(EmuSettings.videoXshift > -50 && EmuSettings.videoXshift < 50))
			EmuSettings.videoXshift = 0;
		if(!(EmuSettings.videoYshift > -50 && EmuSettings.videoYshift < 50))
			EmuSettings.videoYshift = 0;

		char shift[10];
		sprintf(shift, "%hd, %hd", EmuSettings.videoXshift, EmuSettings.videoYshift);
		settingText->setRawText(shift);
		b->resetState();
	}
}

static void ScreenPositionWindowLeftClick(void * ptr) { ScreenPositionWindowUpdate(ptr, -1, 0); }
static void ScreenPositionWindowRightClick(void * ptr) { ScreenPositionWindowUpdate(ptr, +1, 0); }
static void ScreenPositionWindowUpClick(void * ptr) { ScreenPositionWindowUpdate(ptr, 0, -1); }
static void ScreenPositionWindowDownClick(void * ptr) { ScreenPositionWindowUpdate(ptr, 0, +1); }

static void ScreenPositionWindow()
{
	GuiWindow * w = new GuiWindow(150,150);
	w->setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
	w->setPosition(0, -10);

	MenuArrowButton arrowLeftBtn(SHAPE::TRIANGLE_LEFT, ALIGN_H::LEFT, ALIGN_V::MIDDLE, 0, 0, ScreenPositionWindowLeftClick);

	MenuArrowButton arrowRightBtn(SHAPE::TRIANGLE_RIGHT, ALIGN_H::RIGHT, ALIGN_V::MIDDLE, 0, 0, ScreenPositionWindowRightClick);

	MenuArrowButton arrowUpBtn(SHAPE::TRIANGLE_UP, ALIGN_H::CENTRE, ALIGN_V::TOP, 0, 0, ScreenPositionWindowUpClick);

	MenuArrowButton arrowDownBtn(SHAPE::TRIANGLE_DOWN, ALIGN_H::CENTRE, ALIGN_V::BOTTOM, 0, 0, ScreenPositionWindowDownClick);

	MenuScreenShape screenPositionImg;
	screenPositionImg.setAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);

	settingText = new GuiText(nullptr, 20, (PixelColor){0, 0, 0, 255});
	char shift[10];
	sprintf(shift, "%i, %i", EmuSettings.videoXshift, EmuSettings.videoYshift);
	settingText->setRawText(shift);

	int currentX = EmuSettings.videoXshift;
	int currentY = EmuSettings.videoYshift;

	w->append(&arrowLeftBtn);
	w->append(&arrowRightBtn);
	w->append(&arrowUpBtn);
	w->append(&arrowDownBtn);
	w->append(&screenPositionImg);
	w->append(settingText);

	if(!SettingWindow("Screen Position",w))
	{
		// undo changes
		EmuSettings.videoXshift = currentX;
		EmuSettings.videoYshift = currentY;
	}

	delete(w);
	delete(settingText);
}

static int MenuSettingsGeneralMappings()
{
	int selection = MENU_NONE;
	int ret;
	int i = 0;
	bool firstRun = true;
	OptionList options;

	sprintf(options.name[i++], "Menu Toggle");
	sprintf(options.name[i++], "Right Stick as C Buttons");

	options.length = i;

	for(i=0; i < options.length; i++)
		options.value[i][0] = 0;

	UiScope ui;
	GuiLabel& title = ui.label("General Mappings").fontSize(UI_FONT_TITLE - 2).textColor(MenuChromeText());
	GuiLabel& hint = ui.label("The menu toggle and the right stick as the C buttons").fontSize(UI_FONT_SMALL + 2).textColor(MenuChromeTextMuted());
	hint.setMaxWidth(176);
	hint.setScroll(SCROLL::HORIZONTAL);
	GuiLayout& titleBlock = ui.column().gap(4).size(176, 0).add(title).add(hint);
	GuiLayout& header = ui.row().size(platform->getVideo()->getScreenWidth(), MENU_BAR_TOP_H).pad(MENU_PAGE_MARGIN, 0)
		.align(LAYOUT_ALIGN::CENTRE).add(titleBlock);
	header.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	header.setPosition(0, 0);

	GuiTrigger trigB;
	trigB.setSecondaryTrigger();

	MenuButton backBtn(MenuButtonShape::NORMAL, "Go Back", ALIGN_H::LEFT, ALIGN_V::BOTTOM, 50, -35, &trigB);

	GuiOptionBrowser optionBrowser(MENU_OPTIONS_W, MENU_OPTIONS_H, &options);
	optionBrowser.setPosition(0, MENU_LIST_Y);
	optionBrowser.setCol2Position(240);
	optionBrowser.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);

	GuiWindow w(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());
	w.append(&backBtn);
	menu->mainWindow.appendWithAutoRemove(&optionBrowser);
	menu->mainWindow.appendWithAutoRemove(&w);
	menu->mainWindow.appendWithAutoRemove(&header);

	while(selection == MENU_NONE)
	{
		if(!UpdateGui()) return MENU_EXIT;

		ret = optionBrowser.getClickedOption();

		switch (ret)
		{
			case 0:
				EmuSettings.gamepadMenuToggle++;
				if (EmuSettings.gamepadMenuToggle >= GAMEPAD_MENU_TOGGLE_LENGTH)
					EmuSettings.gamepadMenuToggle = GAMEPAD_MENU_TOGGLE_DEFAULT;
				break;

			case 1:
				EmuSettings.mapRightStickToC = !EmuSettings.mapRightStickToC;
				break;
		}

		if(ret >= 0 || firstRun)
		{
			firstRun = false;

			switch(EmuSettings.gamepadMenuToggle)
			{
				case GAMEPAD_MENU_TOGGLE_DEFAULT:
					sprintf (options.value[0], "Default (All Enabled)"); break;
				case GAMEPAD_MENU_TOGGLE_HOME_RIGHTSTICK:
					sprintf (options.value[0], "Home / Right Stick"); break;
				case GAMEPAD_MENU_TOGGLE_LRSTART_12PLUS:
					sprintf (options.value[0], "L+R+Start / 1+2+Plus"); break;
			}

			sprintf (options.value[1], "%s", EmuSettings.mapRightStickToC ? "On" : "Off");

			optionBrowser.triggerUpdate();
		}

		if(backBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_GAMESETTINGS_MAPPINGS;
		}
	}
	return selection;
}

static int MenuSettingsVideo()
{
	int selection = MENU_NONE;
	int ret;
	int i = 0;
	bool firstRun = true;
	OptionList options;

	sprintf(options.name[i++], "Aspect Ratio Correction");
	sprintf(options.name[i++], "Internal Resolution");
	sprintf(options.name[i++], "Bilinear Filtering");
	sprintf(options.name[i++], "Upscaling");
	sprintf(options.name[i++], "Scanline Overlay");
	sprintf(options.name[i++], "Framebuffer Textures");
	sprintf(options.name[i++], "Screen Zoom");
	sprintf(options.name[i++], "Screen Position");
	options.length = i;

	for(i=0; i < options.length; i++)
		options.value[i][0] = 0;

	GuiText titleTxt("Game Settings - Video", 26, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.setPosition(50,50);

	GuiTrigger trigB;
	trigB.setSecondaryTrigger();

	MenuButton backBtn(MenuButtonShape::NORMAL, "Go Back", ALIGN_H::LEFT, ALIGN_V::BOTTOM, 50, -35, &trigB);

	GuiOptionBrowser optionBrowser(MENU_OPTIONS_W, MENU_OPTIONS_H, &options);
	optionBrowser.setPosition(0, MENU_LIST_Y);
	optionBrowser.setCol2Position(240);
	optionBrowser.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);

	GuiWindow w(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());
	w.append(&backBtn);
	menu->mainWindow.appendWithAutoRemove(&optionBrowser);
	menu->mainWindow.appendWithAutoRemove(&w);
	menu->mainWindow.appendWithAutoRemove(&titleTxt);

	while(selection == MENU_NONE)
	{
		if(!UpdateGui()) return MENU_EXIT;

		ret = optionBrowser.getClickedOption();

		switch (ret)
		{
			case 0:
				EmuSettings.videoAspectRatioCorrection++;
				if(EmuSettings.videoAspectRatioCorrection >= VIDEO_ASPECT_RATIO_CORRECTION_LENGTH)
					EmuSettings.videoAspectRatioCorrection = VIDEO_ASPECT_RATIO_CORRECTION_NONE;
				break;

			case 1:
				EmuSettings.renderScale++;
				if(EmuSettings.renderScale >= RENDER_SCALE_LENGTH)
					EmuSettings.renderScale = RENDER_SCALE_1X;
				break;

			case 2:
				EmuSettings.videoBilinearFilter = !EmuSettings.videoBilinearFilter;
				break;

			case 3:
				EmuSettings.videoUpscalingFilter++;
				if (EmuSettings.videoUpscalingFilter >= NUM_UPSCALE_FILTERS)
					EmuSettings.videoUpscalingFilter = UPSCALE_NONE;
				break;

			case 4:
				EmuSettings.videoScanlines = !EmuSettings.videoScanlines;
				break;

			case 5:
				EmuSettings.frameFramebufferTextures = !EmuSettings.frameFramebufferTextures;
				break;

			case 6:
				ScreenZoomWindow();
				break;

			case 7:
				ScreenPositionWindow();
				break;
		}

		if(ret >= 0 || firstRun)
		{
			firstRun = false;

			switch(EmuSettings.videoAspectRatioCorrection)
			{
				case VIDEO_ASPECT_RATIO_CORRECTION_NONE:
					sprintf (options.value[0], "Fill the Screen"); break;
				case VIDEO_ASPECT_RATIO_CORRECTION_4_3:
					sprintf (options.value[0], "4:3"); break;
				case VIDEO_ASPECT_RATIO_CORRECTION_4_3_FIXED:
					sprintf (options.value[0], "4:3 (Fixed Pixel Ratio)"); break;
			}

			switch(EmuSettings.renderScale)
			{
				case RENDER_SCALE_1X: sprintf (options.value[1], "Native (640x480)"); break;
				case RENDER_SCALE_2X: sprintf (options.value[1], "2x (1280x960)"); break;
				case RENDER_SCALE_3X: sprintf (options.value[1], "3x (1920x1440)"); break;
			}

			if (EmuSettings.videoUpscalingFilter == UPSCALE_SHARP_BILINEAR)
				sprintf (options.value[2], "N/A"); // Sharp Bilinear does its own filtering
			else
				sprintf (options.value[2], "%s", EmuSettings.videoBilinearFilter ? "On" : "Off");

			sprintf (options.value[3], "%s", GetUpscaleFilterName(EmuSettings.videoUpscalingFilter));
			sprintf (options.value[4], "%s", EmuSettings.videoScanlines ? "On" : "Off");
			sprintf (options.value[5], "%s", EmuSettings.frameFramebufferTextures ? "On" : "Off");
			sprintf (options.value[6], "%.2f%%, %.2f%%", EmuSettings.videoZoomHor*100, EmuSettings.videoZoomVert*100);
			sprintf (options.value[7], "%d, %d", EmuSettings.videoXshift, EmuSettings.videoYshift);

			optionBrowser.triggerUpdate();
		}

		if(backBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_GAMESETTINGS;
		}
	}
	return selection;
}

/****************************************************************************
 * MenuSettingsEmulation
 ***************************************************************************/
static int MenuSettingsEmulation()
{
	int selection = MENU_NONE;
	int ret;
	int i = 0;
	bool firstRun = true;
	OptionList options;

	sprintf(options.name[i++], "CPU Core");
	sprintf(options.name[i++], "Cycles Per Instruction");
	sprintf(options.name[i++], "Limit Frame Rate");
	sprintf(options.name[i++], "Mute Game Audio");
	sprintf(options.name[i++], "Controller Pak 1");
	sprintf(options.name[i++], "Controller Pak 2");
	sprintf(options.name[i++], "Controller Pak 3");
	sprintf(options.name[i++], "Controller Pak 4");
	options.length = i;

	for(i=0; i < options.length; i++)
		options.value[i][0] = 0;

	GuiText titleTxt("Game Settings - Emulation", 26, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.setPosition(50,50);

	GuiTrigger trigB;
	trigB.setSecondaryTrigger();

	MenuButton backBtn(MenuButtonShape::NORMAL, "Go Back", ALIGN_H::LEFT, ALIGN_V::BOTTOM, 50, -35, &trigB);

	GuiOptionBrowser optionBrowser(MENU_OPTIONS_W, MENU_OPTIONS_H, &options);
	optionBrowser.setPosition(0, MENU_LIST_Y);
	optionBrowser.setCol2Position(240);
	optionBrowser.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);

	GuiWindow w(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());
	w.append(&backBtn);
	menu->mainWindow.appendWithAutoRemove(&optionBrowser);
	menu->mainWindow.appendWithAutoRemove(&w);
	menu->mainWindow.appendWithAutoRemove(&titleTxt);

	while(selection == MENU_NONE)
	{
		if(!UpdateGui()) return MENU_EXIT;
		ret = optionBrowser.getClickedOption();

		switch (ret)
		{
			case 0:
				EmuSettings.cpuCore++;
				if(EmuSettings.cpuCore >= CPUCORE_LENGTH)
					EmuSettings.cpuCore = CPUCORE_AUTO;
				break;

			case 1:
				EmuSettings.countPerOp++;
				if(EmuSettings.countPerOp > 3)
					EmuSettings.countPerOp = 1;
				break;

			case 2:
				EmuSettings.limitFrameRate = !EmuSettings.limitFrameRate;
				break;

			case 3:
				EmuSettings.muteAudio = !EmuSettings.muteAudio;
				break;

			case 4:
			case 5:
			case 6:
			case 7:
			{
				int & pak = EmuSettings.pakMode[ret - 4];
				pak++;
				if(pak >= PAK_LENGTH)
					pak = PAK_NONE;
				break;
			}
		}

		if(ret >= 0)
		{
			N64ApplySettings();
			MarkLiveStateChanged();
		}

		if(ret >= 0 || firstRun)
		{
			firstRun = false;

			switch(EmuSettings.cpuCore)
			{
				case CPUCORE_AUTO:
					sprintf (options.value[0], "Automatic (Recommended)"); break;
				case CPUCORE_DYNAREC:
					sprintf (options.value[0], "Recompiler"); break;
				case CPUCORE_CACHED_INTERPRETER:
					sprintf (options.value[0], "Cached Interpreter"); break;
				case CPUCORE_PURE_INTERPRETER:
					sprintf (options.value[0], "Pure Interpreter"); break;
			}

			sprintf (options.value[1], "%d", EmuSettings.countPerOp);
			sprintf (options.value[2], "%s", EmuSettings.limitFrameRate ? "On" : "Off");
			sprintf (options.value[3], "%s", EmuSettings.muteAudio ? "On" : "Off");

			for(int p = 0; p < 4; p++)
			{
				switch(EmuSettings.pakMode[p])
				{
					case PAK_NONE: sprintf (options.value[4 + p], "None"); break;
					case PAK_MEMORY: sprintf (options.value[4 + p], "Memory Pak"); break;
					case PAK_RUMBLE: sprintf (options.value[4 + p], "Rumble Pak"); break;
				}
			}

			optionBrowser.triggerUpdate();
		}
		if(backBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_GAMESETTINGS;
		}
	}
	return selection;
}

/****************************************************************************
 * MenuSettings
 ***************************************************************************/
static int MenuSettings()
{
	int selection = MENU_NONE;

	GuiText titleTxt("Settings", 26, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.setPosition(50,50);

	MenuIconData iconFile(icon_settings_file_png);
	MenuIconData iconMenu(icon_settings_menu_png);
	MenuIconData iconNetwork(icon_settings_network_png);
	MenuIconData iconCredits(icon_credits_png);

	GuiTrigger trigB;
	trigB.setSecondaryTrigger();

	MenuLargeButton storageBtn("Storage", &iconFile, -125, 120, 30);

	MenuLargeButton menuBtn("Menu", &iconMenu, 125, 120, 30);

	MenuLargeButton networkBtn("Network Share", &iconNetwork, -125, 250, 30);

	MenuLargeButton creditsBtn("Credits", &iconCredits, 125, 250);

	MenuButton backBtn(MenuButtonShape::LONG, "Go Back", ALIGN_H::LEFT, ALIGN_V::BOTTOM, 90, -35, &trigB);

	MenuButton resetBtn(MenuButtonShape::LONG, "Reset Settings", ALIGN_H::RIGHT, ALIGN_V::BOTTOM, -90, -35);

	GuiWindow w(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());
	w.append(&titleTxt);
	w.append(&storageBtn);
	w.append(&menuBtn);
	w.append(&networkBtn);
	w.append(&creditsBtn);
	w.append(&backBtn);
	w.append(&resetBtn);

	menu->mainWindow.appendWithAutoRemove(&w);

	// Coming back from one of its pages: the button that opened it
	switch(previousMenu)
	{
		case MENU_SETTINGS_STORAGE:
			SelectButton(w, storageBtn);
			break;
		case MENU_SETTINGS_MENU:
			SelectButton(w, menuBtn);
			break;
		case MENU_SETTINGS_NETWORK:
			SelectButton(w, networkBtn);
			break;
		default:
			break;
	}


	while(selection == MENU_NONE)
	{
		if(!UpdateGui()) return MENU_EXIT;

		if(storageBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_SETTINGS_STORAGE;
		}
		else if(menuBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_SETTINGS_MENU;
		}
		else if(networkBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_SETTINGS_NETWORK;
		}
		else if(backBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_GAMESELECTION;
		}
		else if(resetBtn.getState() == STATE::CLICKED)
		{
			resetBtn.resetState();

			int choice = WindowPrompt(
				"Reset Settings",
				"Are you sure that you want to reset your settings?",
				"Yes",
				"No");

			if(choice == 1) {
				DefaultSettings();
				ApplySettings();
				RunWithGuiUpdates(AutoDetectDevicesTask);
			}
		}
		else if(creditsBtn.getState() == STATE::CLICKED)
		{
			creditsBtn.resetState();
			CreditsWindow();
		}
	}

	return selection;
}

/****************************************************************************
 * MenuSettingsStorage
 ***************************************************************************/

static int MenuSettingsStorage()
{
	int selection = MENU_NONE;
	int ret;
	int i = 0;
	bool firstRun = true;
	OptionList options;
	sprintf(options.name[i++], "Load Device");
	sprintf(options.name[i++], "Save Device");
	sprintf(options.name[i++], "Load Folder");
	sprintf(options.name[i++], "Save Folder");
	sprintf(options.name[i++], "Screenshots Folder");
	sprintf(options.name[i++], "Covers Folder");
	sprintf(options.name[i++], "Artwork Folder");
	options.length = i;

	for(i=0; i < options.length; i++)
		options.value[i][0] = 0;

	GuiText titleTxt("Settings - Storage", 26, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.setPosition(50,50);

	GuiTrigger trigB;
	trigB.setSecondaryTrigger();

	MenuButton backBtn(MenuButtonShape::LONG, "Go Back", ALIGN_H::LEFT, ALIGN_V::BOTTOM, 90, -35, &trigB);

	GuiOptionBrowser optionBrowser(MENU_OPTIONS_W, MENU_OPTIONS_H, &options);
	optionBrowser.setPosition(0, MENU_LIST_Y);
	optionBrowser.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	optionBrowser.setCol2Position(215);

	GuiWindow w(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());
	w.append(&backBtn);
	menu->mainWindow.appendWithAutoRemove(&optionBrowser);
	menu->mainWindow.appendWithAutoRemove(&w);
	menu->mainWindow.appendWithAutoRemove(&titleTxt);

	while(selection == MENU_NONE)
	{
		if(!UpdateGui()) return MENU_EXIT;

		ret = optionBrowser.getClickedOption();

		switch (ret)
		{
			case 0:
				EmuSettings.loadDevice = getNextLoadDevice(EmuSettings.loadDevice);
				break;

			case 1:
				EmuSettings.saveDevice = getNextSaveDevice(EmuSettings.saveDevice);
				break;

			case 2:
				OnScreenKeyboard(EmuSettings.loadFolder, MAXPATHLEN);
				break;

			case 3:
				OnScreenKeyboard(EmuSettings.saveFolder, MAXPATHLEN);
				break;

			case 4:
				OnScreenKeyboard(EmuSettings.screenshotsFolder, MAXPATHLEN);
				break;

			case 5:
				OnScreenKeyboard(EmuSettings.coverFolder, MAXPATHLEN);
				break;

			case 6:
				OnScreenKeyboard(EmuSettings.artworkFolder, MAXPATHLEN);
				break;
		}

		if(ret >= 0 || firstRun)
		{
			firstRun = false;

			if (EmuSettings.loadDevice == DEVICE_AUTO) sprintf (options.value[0],"Auto Detect");
			else if (EmuSettings.loadDevice == DEVICE_SD) sprintf (options.value[0],"SD");
			else if (EmuSettings.loadDevice == DEVICE_USB) sprintf (options.value[0],"USB");
			else if (EmuSettings.loadDevice == DEVICE_DVD) sprintf (options.value[0],"DVD");
			else if (EmuSettings.loadDevice == DEVICE_SMB) sprintf (options.value[0],"Network Share");
			else if (EmuSettings.loadDevice == DEVICE_SD_SLOTA) sprintf (options.value[0],"SD Gecko Slot A");
			else if (EmuSettings.loadDevice == DEVICE_SD_SLOTB) sprintf (options.value[0],"SD Gecko Slot B");
			else if (EmuSettings.loadDevice == DEVICE_SD_PORT2) sprintf (options.value[0],"SD in SP2");
			else if (EmuSettings.loadDevice == DEVICE_SD_GCLOADER) sprintf (options.value[0],"GC Loader");

			if (EmuSettings.saveDevice == DEVICE_AUTO) sprintf (options.value[1],"Auto Detect");
			else if (EmuSettings.saveDevice == DEVICE_SD) sprintf (options.value[1],"SD");
			else if (EmuSettings.saveDevice == DEVICE_USB) sprintf (options.value[1],"USB");
			else if (EmuSettings.saveDevice == DEVICE_SMB) sprintf (options.value[1],"Network Share");
			else if (EmuSettings.saveDevice == DEVICE_SD_SLOTA) sprintf (options.value[1],"SD Gecko Slot A");
			else if (EmuSettings.saveDevice == DEVICE_SD_SLOTB) sprintf (options.value[1],"SD Gecko Slot B");
			else if (EmuSettings.saveDevice == DEVICE_SD_PORT2) sprintf (options.value[1],"SD in SP2");
			else if (EmuSettings.saveDevice == DEVICE_SD_GCLOADER) sprintf (options.value[1],"GC Loader");

			CopyString(options.value[2], 35, EmuSettings.loadFolder);
			CopyString(options.value[3], 35, EmuSettings.saveFolder);
			CopyString(options.value[4], 35, EmuSettings.screenshotsFolder);
			CopyString(options.value[5], 35, EmuSettings.coverFolder);
			CopyString(options.value[6], 35, EmuSettings.artworkFolder);

			optionBrowser.triggerUpdate();
		}

		if(backBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_SETTINGS;
			RunWithGuiUpdates(AutoDetectDevicesTask);
		}
	}
	return selection;
}

static bool LoadLanguage()
{
	const uint8_t *buffer;
	size_t size;

	switch(EmuSettings.language)
	{
		case LANG_JAPANESE: buffer = jp_mo; size = jp_mo_size; break;
		case LANG_ENGLISH: textTranslator->clear(); return true; // English is the text in the program
		case LANG_GERMAN: buffer = de_mo; size = de_mo_size; break;
		case LANG_FRENCH: buffer = fr_mo; size = fr_mo_size; break;
		case LANG_SPANISH: buffer = es_mo; size = es_mo_size; break;
		case LANG_ITALIAN: buffer = it_mo; size = it_mo_size; break;
		case LANG_DUTCH: buffer = nl_mo; size = nl_mo_size; break;
		case LANG_SIMP_CHINESE:
		case LANG_TRAD_CHINESE: buffer = zh_mo; size = zh_mo_size; break;
		case LANG_KOREAN: buffer = ko_mo; size = ko_mo_size; break;
		case LANG_PORTUGUESE: buffer = pt_mo; size = pt_mo_size; break;
		case LANG_BRAZILIAN_PORTUGUESE: buffer = pt_br_mo; size = pt_br_mo_size; break;
		case LANG_CATALAN: buffer = ca_mo; size = ca_mo_size; break;
		case LANG_TURKISH: buffer = tr_mo; size = tr_mo_size; break;
		case LANG_SWEDISH: buffer = sv_mo; size = sv_mo_size; break;
		default: return false;
	}

	textTranslator->loadLanguage(buffer, size);
	return true;
}

static void ResetText()
{
	LoadLanguage();

	if(menu)
	{
		menu->mainWindow.resetText();
	}
}

#ifndef HW_DOL
static void GetFontFilePath(char *filepath, size_t size, const char *fontName)
{
	snprintf(filepath, size, "%s/%s", appPath, fontName);
#ifdef __WIIU__
	struct stat st;
	if(appPath[0] != 0 && stat(filepath, &st) == 0)
		return;

	static const char * const folders[] = { "wiiu/apps/" APPFOLDER, APPFOLDER };

	for(const char * folder : folders)
	{
		char candidate[MAXPATHLEN];
		if(!platform->getFileSystem()->getPath(candidate, DEVICE_SD, folder, fontName))
			continue;

		if(stat(candidate, &st) == 0)
		{
			snprintf(filepath, size, "%s", candidate);
			return;
		}
	}
#endif
}
#endif

void ChangeLanguage() {
	if(currentLanguage == EmuSettings.language) {
		return;
	}

	if(EmuSettings.language == LANG_JAPANESE || EmuSettings.language == LANG_KOREAN || EmuSettings.language == LANG_SIMP_CHINESE) {
#ifndef HW_DOL
		char filepath[MAXPATHLEN];

		switch(EmuSettings.language) {
			case LANG_KOREAN:
				GetFontFilePath(filepath, sizeof(filepath), "ko.ttf");
				break;
			case LANG_JAPANESE:
				GetFontFilePath(filepath, sizeof(filepath), "jp.ttf");
				break;
			case LANG_SIMP_CHINESE:
				GetFontFilePath(filepath, sizeof(filepath), "zh.ttf");
				break;
		}

		size_t fileSize = LoadFont(filepath);

		if(fileSize > 0) {
			if(fontSystem) delete fontSystem;
			fontSystem = new GuiTextRenderer(ext_font_ttf, fileSize, platform->getVideo()->getGlyphRenderer(), platform->getVideo()->getUIScale());
		}
		else {
			EmuSettings.language = (currentLanguage >= 0) ? currentLanguage : LANG_ENGLISH;
		}
#else
	EmuSettings.language = (currentLanguage >= 0) ? currentLanguage : LANG_ENGLISH;
	ErrorPrompt("Unsupported language!");
#endif
	}
#ifndef HW_DOL
	else {
		if(ext_font_ttf != nullptr) {
			if(fontSystem) delete fontSystem;
			extmem_free(ext_font_ttf);
			ext_font_ttf = nullptr;
			fontSystem = new GuiTextRenderer(font_ttf, font_ttf_size, platform->getVideo()->getGlyphRenderer(), platform->getVideo()->getUIScale());
		}
	}
#endif
	ResetText();
	currentLanguage = EmuSettings.language;
}

/****************************************************************************
 * MenuSettingsMenu
 ***************************************************************************/

static int MenuSettingsMenu()
{
	int selection = MENU_NONE;
	int ret;
	int i = 0;
	bool firstRun = true;
	OptionList options;
	currentLanguage = EmuSettings.language;

#if defined(HW_RVL) || defined(HW_DOL)
	sprintf(options.name[i++], "Exit Action");
#else
	options.name[i++][0] = 0; // Exit Action not available on this platform
#endif
	sprintf(options.name[i++], "Wiimote Orientation");
	sprintf(options.name[i++], "Music Volume");
	sprintf(options.name[i++], "Sound Effects Volume");
	sprintf(options.name[i++], "Rumble");
	sprintf(options.name[i++], "Language");
	sprintf(options.name[i++], "Preview Image");
	options.length = i;

	for(i=0; i < options.length; i++)
		options.value[i][0] = 0;

	GuiText titleTxt("Settings - Menu", 26, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.setPosition(50,50);

	GuiTrigger trigB;
	trigB.setSecondaryTrigger();

	MenuButton backBtn(MenuButtonShape::LONG, "Go Back", ALIGN_H::LEFT, ALIGN_V::BOTTOM, 90, -35, &trigB);

	GuiOptionBrowser optionBrowser(MENU_OPTIONS_W, MENU_OPTIONS_H, &options);
	optionBrowser.setPosition(0, MENU_LIST_Y);
	optionBrowser.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	optionBrowser.setCol2Position(275);

	GuiWindow w(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());
	w.append(&backBtn);
	menu->mainWindow.appendWithAutoRemove(&optionBrowser);
	menu->mainWindow.appendWithAutoRemove(&w);
	menu->mainWindow.appendWithAutoRemove(&titleTxt);

	while(selection == MENU_NONE)
	{
		if(!UpdateGui()) return MENU_EXIT;

		ret = optionBrowser.getClickedOption();

		switch (ret)
		{
			case 0:
				EmuSettings.exitAction++;
				#ifdef HW_RVL
				if(EmuSettings.exitAction >= EXITACTION_WII_LENGTH)
					EmuSettings.exitAction = EXITACTION_WII_AUTO;
				#elif HW_DOL
				if(EmuSettings.exitAction >= EXITACTION_GC_LENGTH)
					EmuSettings.exitAction = EXITACTION_GC_RETURN_TO_LOADER;
				#endif
				break;
			case 1:
				EmuSettings.wiimoteOrientation++;
				if(EmuSettings.wiimoteOrientation >= WIIMOTE_ORIENTATION_LENGTH)
					EmuSettings.wiimoteOrientation = WIIMOTE_ORIENTATION_VERTICAL;
				platform->getInput()->setWiimoteOrientation(EmuSettings.wiimoteOrientation);
				break;
			case 2:
				EmuSettings.musicVolume += 10;
				if(EmuSettings.musicVolume > 100)
					EmuSettings.musicVolume = 0;
				GuiSound::setDefaultVolume(VOLUME_TYPE::MUSIC, EmuSettings.musicVolume);
				break;
			case 3:
				EmuSettings.sfxVolume += 10;
				if(EmuSettings.sfxVolume > 100)
					EmuSettings.sfxVolume = 0;
				GuiSound::setDefaultVolume(VOLUME_TYPE::SFX, EmuSettings.sfxVolume);
				break;
			case 4:
				EmuSettings.rumble = !EmuSettings.rumble;
				platform->getInput()->setRumbleEnabled(EmuSettings.rumble);
				break;
			case 5:
				EmuSettings.language++;
				
				if(EmuSettings.language == LANG_TRAD_CHINESE) // skip (not supported)
					EmuSettings.language = LANG_KOREAN;
				else if(EmuSettings.language >= LANG_LENGTH)
					EmuSettings.language = LANG_JAPANESE;
				break;
			case 6:
				EmuSettings.previewImage++;
				if(EmuSettings.previewImage >= PREVIEWIMAGE_LENGTH)
					EmuSettings.previewImage = PREVIEWIMAGE_SCREENSHOT;
				break;
		}

		if(ret >= 0 || firstRun)
		{
			firstRun = false;

			#ifdef HW_RVL
			if (EmuSettings.exitAction == EXITACTION_WII_RETURN_TO_MENU)
				sprintf (options.value[0], "Return to Wii Menu");
			else if (EmuSettings.exitAction == EXITACTION_WII_POWER_OFF)
				sprintf (options.value[0], "Power Off Wii");
			else if (EmuSettings.exitAction == EXITACTION_WII_RETURN_TO_LOADER)
				sprintf (options.value[0], "Return to Loader");
			else
				sprintf (options.value[0], "Auto");
			#elif HW_DOL // GameCube
			if (EmuSettings.exitAction == EXITACTION_GC_RETURN_TO_LOADER)
				sprintf (options.value[0], "Return to Loader");
			else
				sprintf (options.value[0], "Reboot");

			options.name[1][0] = 0; // Wiimote
			options.name[2][0] = 0; // Music
			options.name[3][0] = 0; // Sound Effects
			#endif

			if (EmuSettings.wiimoteOrientation == WIIMOTE_ORIENTATION_HORIZONTAL)
				sprintf (options.value[1], "Horizontal");
			else
				sprintf (options.value[1], "Vertical");

			if(EmuSettings.musicVolume > 0)
				sprintf(options.value[2], "%d%%", EmuSettings.musicVolume);
			else
				sprintf(options.value[2], "Mute");

			if(EmuSettings.sfxVolume > 0)
				sprintf(options.value[3], "%d%%", EmuSettings.sfxVolume);
			else
				sprintf(options.value[3], "Mute");

			if (EmuSettings.rumble)
				sprintf (options.value[4], "Enabled");
			else
				sprintf (options.value[4], "Disabled");
			
			switch(EmuSettings.language)
			{
				case LANG_JAPANESE:		sprintf(options.value[5], "Japanese"); break;
				case LANG_ENGLISH:		sprintf(options.value[5], "English"); break;
				case LANG_GERMAN:		sprintf(options.value[5], "German"); break;
				case LANG_FRENCH:		sprintf(options.value[5], "French"); break;
				case LANG_SPANISH:		sprintf(options.value[5], "Spanish"); break;
				case LANG_ITALIAN:		sprintf(options.value[5], "Italian"); break;
				case LANG_DUTCH:		sprintf(options.value[5], "Dutch"); break;
				case LANG_SIMP_CHINESE:	sprintf(options.value[5], "Chinese (Simplified)"); break;
				case LANG_TRAD_CHINESE:	sprintf(options.value[5], "Chinese (Traditional)"); break;
				case LANG_KOREAN:		sprintf(options.value[5], "Korean"); break;
				case LANG_PORTUGUESE:	sprintf(options.value[5], "Portuguese"); break;
				case LANG_BRAZILIAN_PORTUGUESE: sprintf(options.value[5], "Brazilian Portuguese"); break;
				case LANG_CATALAN:		sprintf(options.value[5], "Catalan"); break;
				case LANG_TURKISH:		sprintf(options.value[5], "Turkish"); break;
				case LANG_SWEDISH:		sprintf(options.value[5], "Swedish"); break;
			}
			
			switch(EmuSettings.previewImage)
			{
				case 0:	
					sprintf(options.value[6], "Screenshots");
					break; 
				case 1:	
					sprintf(options.value[6], "Covers");
					break; 
				case 2:	
					sprintf(options.value[6], "Artwork");
					break; 
			}
			
			optionBrowser.triggerUpdate();
		}

		if(backBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_SETTINGS;
		}
	}
	ChangeLanguage();
	return selection;
}

/****************************************************************************
 * MenuSettingsNetwork
 ***************************************************************************/

static int MenuSettingsNetwork()
{
	int selection = MENU_NONE;
	int ret;
	int i = 0;
	bool firstRun = true;
	OptionList options;
	sprintf(options.name[i++], "IP");
	sprintf(options.name[i++], "Name");
	sprintf(options.name[i++], "Username");
	sprintf(options.name[i++], "Password");
	options.length = i;

	for(i=0; i < options.length; i++)
		options.value[i][0] = 0;

	GuiText titleTxt("Settings - Network Share", 26, (PixelColor){255, 255, 255, 255});
	titleTxt.setAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
	titleTxt.setPosition(50,50);

	GuiTrigger trigB;
	trigB.setSecondaryTrigger();

	MenuButton backBtn(MenuButtonShape::LONG, "Go Back", ALIGN_H::LEFT, ALIGN_V::BOTTOM, 90, -35, &trigB);

	GuiOptionBrowser optionBrowser(MENU_OPTIONS_W, MENU_OPTIONS_H, &options);
	optionBrowser.setPosition(0, MENU_LIST_Y);
	optionBrowser.setAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
	optionBrowser.setCol2Position(290);

	GuiWindow w(platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());
	w.append(&backBtn);
	menu->mainWindow.appendWithAutoRemove(&optionBrowser);
	menu->mainWindow.appendWithAutoRemove(&w);
	menu->mainWindow.appendWithAutoRemove(&titleTxt);

	while(selection == MENU_NONE)
	{
		if(!UpdateGui()) return MENU_EXIT;

		ret = optionBrowser.getClickedOption();

		switch (ret)
		{
			case 0:
				OnScreenKeyboard(EmuSettings.smbShare.host, sizeof(EmuSettings.smbShare.host));
				break;

			case 1:
				OnScreenKeyboard(EmuSettings.smbShare.share, sizeof(EmuSettings.smbShare.share));
				break;

			case 2:
				OnScreenKeyboard(EmuSettings.smbShare.user, sizeof(EmuSettings.smbShare.user));
				break;

			case 3:
				OnScreenKeyboard(EmuSettings.smbShare.password, sizeof(EmuSettings.smbShare.password));
				break;
		}

		if(ret >= 0 || firstRun)
		{
			firstRun = false;
			CopyString(options.value[0], 25, EmuSettings.smbShare.host);
			CopyString(options.value[1], 19, EmuSettings.smbShare.share);
			CopyString(options.value[2], 19, EmuSettings.smbShare.user);
			CopyString(options.value[3], 19, EmuSettings.smbShare.password);
			optionBrowser.triggerUpdate();
		}

		if(backBtn.getState() == STATE::CLICKED)
		{
			selection = MENU_SETTINGS;
		}
	}
	CloseShare();
	return selection;
}

// Context passed to BlurredOverlayPixelSource() via fillTexture()'s userdata.
// Holds everything needed to compute the final (post vertical-blur, post
// overlay) color of a single destination pixel, without ever having a
// full-screen plain RGBA8 buffer materialized anywhere.
struct BlurredGameTextureContext {
	const uint8_t * scaledImg; // horizontally-blurred crop, cropWidth x cropHeight, RGBA8
	int cropWidth;
	int cropHeight;
	int drawX;
	int drawY;
	int blurAmount;
	int div;
	PixelColor overlayColor;
	uint8_t bgR, bgG, bgB, bgA; // precomputed flat background (black + overlay)
};

// ImageRenderer::PixelSourceFn implementation: does the vertical blur pass
// and overlay blend for destination pixel (x,y), sourcing from the
// horizontally-blurred crop buffer, or returns the flat background color
// for anything outside the crop/screen bounds.
static void BlurredOverlayPixelSource(int x, int y, PixelColor * outColor, void * userdata)
{
	BlurredGameTextureContext * ctx = (BlurredGameTextureContext *)userdata;

	if (x >= ctx->drawX && x < ctx->drawX + ctx->cropWidth &&
		y >= ctx->drawY && y < ctx->drawY + ctx->cropHeight) {

		int cx = x - ctx->drawX;
		int cy = y - ctx->drawY;

		int sumR = 0, sumG = 0, sumB = 0;
		for (int k = -ctx->blurAmount; k <= ctx->blurAmount; ++k) {
			int ny = cy + k;
			if (ny < 0) ny = 0;
			if (ny >= ctx->cropHeight) ny = ctx->cropHeight - 1;

			int idx = (ny * ctx->cropWidth + cx) * 4;
			sumR += ctx->scaledImg[idx + 0];
			sumG += ctx->scaledImg[idx + 1];
			sumB += ctx->scaledImg[idx + 2];
		}

		uint8_t blurredR = sumR / ctx->div;
		uint8_t blurredG = sumG / ctx->div;
		uint8_t blurredB = sumB / ctx->div;

		int alphaIn = ctx->overlayColor.a;
		int invAlpha = 255 - alphaIn;

		outColor->r = (uint8_t)((blurredR * invAlpha + ctx->overlayColor.r * alphaIn) / 255);
		outColor->g = (uint8_t)((blurredG * invAlpha + ctx->overlayColor.g * alphaIn) / 255);
		outColor->b = (uint8_t)((blurredB * invAlpha + ctx->overlayColor.b * alphaIn) / 255);
		outColor->a = 255;
	} else {
		outColor->r = ctx->bgR;
		outColor->g = ctx->bgG;
		outColor->b = ctx->bgB;
		outColor->a = ctx->bgA;
	}
}

static void * CreateBlurredGameTexture() {
	if(gameScreenPng.size == 0) {
		return nullptr;
	}

	int blurAmount = 4; // blur amount
	PixelColor blurOverlayColor = (PixelColor){50, 50, 50, 160};

	int screenWidth = platform->getVideo()->getScreenWidth();
	int screenHeight = platform->getVideo()->getScreenHeight();

	int scaledWidth = (int)(gameScreenPng.width * gameScreenPng.scaleX);
	int scaledHeight = (int)(gameScreenPng.height * gameScreenPng.scaleY);

	// Failsafe for invalid scale metrics
	if (scaledWidth <= 0 || scaledHeight <= 0) {
		return nullptr;
	}

	// Calculate the absolute top-left starting pixel of the scaled image.
	int targetCenterX = (screenWidth / 2) + gameScreenPng.xoffset;
	int targetCenterY = (screenHeight / 2) + gameScreenPng.yoffset;

	int trueOffsetX = targetCenterX - (scaledWidth / 2);
	int trueOffsetY = targetCenterY - (scaledHeight / 2);

	// --- VIEWABLE AREA CLAMPING LOGIC ---
	// Determine where to start drawing on the screen bounds
	int drawX = trueOffsetX < 0 ? 0 : trueOffsetX;
	int drawY = trueOffsetY < 0 ? 0 : trueOffsetY;

	// Determine the max visible boundaries clipped to screen dimensions
	int endX = (trueOffsetX + scaledWidth > screenWidth) ? screenWidth : (trueOffsetX + scaledWidth);
	int endY = (trueOffsetY + scaledHeight > screenHeight) ? screenHeight : (trueOffsetY + scaledHeight);

	// Calculate the dimensions of the viewable (cropped) area
	int cropWidth = endX - drawX;
	int cropHeight = endY - drawY;

	// Failsafe if the image is pushed entirely off-screen
	if (cropWidth <= 0 || cropHeight <= 0) {
		return nullptr;
	}

	// Determine the starting offset within the theoretical scaled image
	int cropStartX = trueOffsetX < 0 ? -trueOffsetX : 0;
	int cropStartY = trueOffsetY < 0 ? -trueOffsetY : 0;

	// Allocate texture before scratch memory to avoid fragmentation
	void * texture = platform->getVideo()->getImageRenderer()->createTexture(screenWidth, screenHeight);
	if (!texture) {
		return nullptr;
	}

	uint8_t *src = DecodePNGToRGBA8(gameScreenPng.buffer, gameScreenPng.width, gameScreenPng.height);
	if(!src) {
		platform->getVideo()->getImageRenderer()->destroyTexture(texture);
		return nullptr;
	}

	// Allocate scratch space ONLY for the viewable cropped portion
	uint8_t *scaledImg = (uint8_t *)extmem_malloc(cropWidth * cropHeight * 4);
	uint8_t *rowBuf    = (uint8_t *)extmem_malloc(cropWidth * 4);

	if (!scaledImg || !rowBuf) {
		if (scaledImg) extmem_free(scaledImg);
		if (rowBuf) extmem_free(rowBuf);
		extmem_free(src);
		platform->getVideo()->getImageRenderer()->destroyTexture(texture);
		return nullptr;
	}

	// Scale the raw input PNG directly into our viewable cropped buffer
	for (int dy = 0; dy < cropHeight; ++dy) {
		int scaledImgY = cropStartY + dy;
		int sy = (scaledImgY * gameScreenPng.height) / scaledHeight;
		if (sy < 0) sy = 0;
		if (sy >= gameScreenPng.height) sy = gameScreenPng.height - 1;

		for (int dx = 0; dx < cropWidth; ++dx) {
			int scaledImgX = cropStartX + dx;
			int sx = (scaledImgX * gameScreenPng.width) / scaledWidth;
			if (sx < 0) sx = 0;
			if (sx >= gameScreenPng.width) sx = gameScreenPng.width - 1;

			int srcIdx = (sy * gameScreenPng.width + sx) * 4;
			int dstIdx = (dy * cropWidth + dx) * 4;

			scaledImg[dstIdx + 0] = src[srcIdx + 0];
			scaledImg[dstIdx + 1] = src[srcIdx + 1];
			scaledImg[dstIdx + 2] = src[srcIdx + 2];
			scaledImg[dstIdx + 3] = src[srcIdx + 3];
		}
	}

	int div = 2 * blurAmount + 1;

	// Horizontal Box Blur Pass (in-place using the small rowBuf)
	for (int y = 0; y < cropHeight; ++y) {
		memcpy(rowBuf, &scaledImg[y * cropWidth * 4], cropWidth * 4);

		for (int x = 0; x < cropWidth; ++x) {
			int sumR = 0, sumG = 0, sumB = 0;

			for (int k = -blurAmount; k <= blurAmount; ++k) {
				int nx = x + k;
				if (nx < 0) nx = 0;
				if (nx >= cropWidth) nx = cropWidth - 1;

				int idx = nx * 4;
				sumR += rowBuf[idx + 0];
				sumG += rowBuf[idx + 1];
				sumB += rowBuf[idx + 2];
			}

			int dstIdx = (y * cropWidth + x) * 4;
			scaledImg[dstIdx + 0] = sumR / div;
			scaledImg[dstIdx + 1] = sumG / div;
			scaledImg[dstIdx + 2] = sumB / div;
		}
	}

	// The horizontally-blurred crop (scaledImg) and rowBuf are all we need from
	// here on.
	extmem_free(rowBuf);
	extmem_free(src);

	// Precalculate flat background color (Solid Black + Overlay) once, rather
	// than per background pixel.
	int alphaIn = blurOverlayColor.a;
	int invAlpha = 255 - alphaIn;

	BlurredGameTextureContext ctx;
	ctx.scaledImg = scaledImg;
	ctx.cropWidth = cropWidth;
	ctx.cropHeight = cropHeight;
	ctx.drawX = drawX;
	ctx.drawY = drawY;
	ctx.blurAmount = blurAmount;
	ctx.div = div;
	ctx.overlayColor = blurOverlayColor;
	ctx.bgR = (uint8_t)((0 * invAlpha + blurOverlayColor.r * alphaIn) / 255);
	ctx.bgG = (uint8_t)((0 * invAlpha + blurOverlayColor.g * alphaIn) / 255);
	ctx.bgB = (uint8_t)((0 * invAlpha + blurOverlayColor.b * alphaIn) / 255);
	ctx.bgA = 255;

	// Vertical blur + overlay compositing happens here, in platform-agnostic
	// pixel math (BlurredOverlayPixelSource). The platform driver's only job
	// is to place each resulting pixel into its own native texture layout -
	// this function still only ever holds the small cropped scratch buffer,
	// never a full-screen plain RGBA8 intermediate.
	platform->getVideo()->getImageRenderer()->fillTexture(texture, screenWidth, screenHeight, BlurredOverlayPixelSource, &ctx);

	extmem_free(scaledImg);
	return texture;
}

/****************************************************************************
 * MainMenu
 ***************************************************************************/
static int FirstRunTask(void *) {
	AutoDetectDevices();
	CreateMissingDirectories();
	SavePrefs();
	return 0;
}

void MainMenu (int selection)
{
	static bool firstRun = true;
	int currentMenu = selection;
	lastMenu = MENU_NONE;
	previousMenu = MENU_NONE;
	
	if(firstRun)
	{
		#ifndef HW_DOL
		pointer[0] = new GuiImageData(player1_point_png);
		pointer[1] = new GuiImageData(player2_point_png);
		pointer[2] = new GuiImageData(player3_point_png);
		pointer[3] = new GuiImageData(player4_point_png);
		for(int i = 0; i < 4; i++)
			cursorImg[i].setImage(pointer[i]);
		#endif

		trigA = new GuiTrigger;
		trigA->setPrimaryTrigger();
		MenuPrimaryTrigger() = trigA;

		// the sounds every button plays; registered once, so pages don't build their own
		#ifndef HW_DOL
		static GuiSound buttonSoundOver(button_over_pcm, button_over_pcm_size, SOUND::PCM);
		static GuiSound buttonSoundClick(button_click_pcm, button_click_pcm_size, SOUND::PCM);
		GuiSound::setDefaultSounds(&buttonSoundOver, &buttonSoundClick);
		#endif
	}

	if(selection == MENU_GAME)
	{
		gameScreenTexture = CreateBlurredGameTexture();
		if(gameScreenTexture != nullptr) {
			gameScreenImg = new GuiImage((uint8_t *)gameScreenTexture, platform->getVideo()->getScreenWidth(), platform->getVideo()->getScreenHeight());
		}
	}

	if(gameScreenImg == nullptr) {
		gameScreenImg = new MenuBackgroundShape();
	}

	std::unique_ptr<Menu> menuInstance = std::make_unique<Menu>();
	menu = menuInstance.get();

	if(firstRun) {
		firstRun = false;

		LoadPrefs();

		if(RunOnWorkerThread(FirstRunTask)) {
			while(!IsWorkerThreadFinished()) {
				if(!UpdateGui()) break;
			}
		}

		#ifdef HW_RVL
		uint32_t ios = IOS_GetVersion();

		if(!SupportedIOS(ios))
			ErrorPrompt("The current IOS is unsupported. Functionality and/or stability may be adversely affected.");
		else if(!SaneIOS(ios))
			ErrorPrompt("The current IOS has been altered (fake-signed). Functionality and/or stability may be adversely affected.");
		#endif

		#ifdef HW_DOL
		bgMusic = new GuiSound();
		enterSound = new GuiSound();
		exitSound = new GuiSound();
		#else
		bgMusic = new GuiSound(bg_music, bg_music_size, SOUND::OGG);
		bgMusic->setLoop(true);
		enterSound = new GuiSound(enter_ogg, enter_ogg_size, SOUND::OGG);
		exitSound = new GuiSound(exit_ogg, exit_ogg_size, SOUND::OGG);
		#endif
	}

	if(currentMenu == MENU_GAMESELECTION)
		bgMusic->play();

	while(currentMenu != MENU_EXIT || !N64IsLoaded())
	{
		const int pageRun = currentMenu;
		switch (currentMenu)
		{
			case MENU_GAMESELECTION:
				currentMenu = MenuGameSelection();
				break;
			case MENU_GAME:
				currentMenu = MenuGame();
				break;
			case MENU_GAME_LOAD:
				currentMenu = MenuGameSaves();
				break;
			case MENU_GAME_SAVE:
				currentMenu = MenuGameSaves();
				break;
			case MENU_GAME_DELETE:
				currentMenu = MenuGameSaves();
				break;	
			case MENU_GAMESETTINGS:
				currentMenu = MenuGameSettings();
				break;
			case MENU_GAMESETTINGS_MAPPINGS:
				currentMenu = MappingScreen();
				break;
			case MENU_GAMESETTINGS_VIDEO:
				currentMenu = MenuSettingsVideo();
				break;
			case MENU_GAMESETTINGS_EMULATION:
				currentMenu = MenuSettingsEmulation();
				break;
			case MENU_SETTINGS:
				currentMenu = MenuSettings();
				break;
			case MENU_SETTINGS_STORAGE:
				currentMenu = MenuSettingsStorage();
				break;
			case MENU_SETTINGS_MENU:
				currentMenu = MenuSettingsMenu();
				break;
			case MENU_SETTINGS_NETWORK:
				currentMenu = MenuSettingsNetwork();
				break;
			case MENU_GAMESETTINGS_MAPPINGS_GENERAL:
				currentMenu = MenuSettingsGeneralMappings();
				break;
			default: // unrecognized menu
				currentMenu = MenuGameSelection();
				break;
		}
		lastMenu = currentMenu;
		previousMenu = pageRun;

		if(guiExiting)
			break;
	}

	CancelAction();

	if(gameScreenImg != nullptr) {
		menuInstance->mainWindow.remove(gameScreenImg);
		delete gameScreenImg;
		gameScreenImg = nullptr;
	}

	if(gameScreenTexture != nullptr) {
		platform->getVideo()->getImageRenderer()->destroyTexture(gameScreenTexture);
		gameScreenTexture = nullptr;
	}

	ClearScreenshot();

	// wait for keys to be depressed - but not when leaving the app
	while(isMenuRequested() && !platform->isExiting())
	{
		platform->getInput()->update();
		usleep(THREAD_SLEEP);
	}
	
	menu = nullptr;

	// Give back the shape meshes the menu cached (including the video backend's copies) before
	// the game runs
	menuInstance.reset();
	GuiMeshCache::flush();
}
