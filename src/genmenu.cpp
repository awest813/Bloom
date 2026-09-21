// SPDX-License-Identifier: GPL-2.0-only
/*
 * Emulator's GUI
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

extern "C" {
#include <math.h>
}

#include <kos/fs.h>

#include <tsu/genmenu.h>
#include <tsu/font.h>

#include <tsu/drawables/label.h>
#include <tsu/anims/logxymover.h>
#include <tsu/anims/expxymover.h>
#include <tsu/anims/alphafader.h>
#include <tsu/triggers/death.h>

#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "background.h"
#include "bloom-config.h"
#include "emu.h"
#include "genmenu.h"
#include "menu_util.h"
#include "revision.h"
#include "settings.h"

#define SCREEN_W 640
#define TITLE_Y 32
#define SUBTITLE_Y 58
#define MAIN_LIST_Y 128
#define LIST_Y 92
#define LIST_BOTTOM 400
#define STATUS_Y 418
#define HINT_Y 448

#define MENU_ENTRY_SIZE 32
#define ENTRY_SIZE 20
#define CREDITS_ENTRY_SIZE 12

#define TOP_PATH "/"

static fs::path last_browse = TOP_PATH;

static bool is_cd_image_ext(const std::string &ext)
{
	return menu_is_cd_image_ext(ext.c_str(), WITH_CHD);
}

static std::string trunc_label(const std::string &text, size_t max_chars)
{
	char buf[128];

	menu_truncate(buf, sizeof(buf), text.c_str(), max_chars);
	return buf;
}

static fs::path usable_browse_path(fs::path path)
{
	std::error_code error;

	if (path.empty() || path == TOP_PATH)
		return TOP_PATH;
	if (!menu_path_allowed(path.c_str()))
		return TOP_PATH;
	if (fs::is_directory(path, error))
		return path;
	path = path.parent_path();
	if (!path.empty() && path != TOP_PATH && menu_path_allowed(path.c_str())
	    && fs::is_directory(path, error))
		return path;
	return TOP_PATH;
}

static std::shared_ptr<MyMenu> myMenu;

MyLabel::MyLabel(std::shared_ptr<Font> fh, const std::string& text, int size,
		 bool centered, const Color& selected, const Color& deselected) :
	Label(fh, "", size, centered, true),
	m_color_selected(selected),
	m_color_deselected(deselected),
	m_font(fh), m_size(size)
{
	m_label = text;

	setText(m_label);
	deselect();
}

void MyLabel::draw(int list)
{
	const Vector &p = getPosition();

	if (p.y <= 480 + (int)m_size && p.y >= -(int)m_size)
		Label::draw(list);
}

class AnimFadeAway : public Animation {
public:
	AnimFadeAway(bool vertical, float delta, float max, const Action &action) {
		m_delta = delta;
		m_max = max;
		m_vertical = vertical;
		m_action = action;
	}

	virtual ~AnimFadeAway() {}

protected:
	virtual void complete(Drawable *t) { m_action(); }
	virtual void nextFrame(Drawable *t);

private:
	bool m_vertical;
	float m_delta, m_max;
	Action m_action;
};

class AnimFadeIn : public Animation {
public:
	AnimFadeIn(bool vertical, float max, const Action &action) {
		m_max = max;
		m_vertical = vertical;
		m_action = action;
	}

	virtual ~AnimFadeIn() {}

protected:
	virtual void complete(Drawable *t) { m_action(); }
	virtual void nextFrame(Drawable *t);

private:
	bool m_vertical;
	float m_delta, m_max;
	Action m_action;
};

void PathLabel::activate()
{
	const std::string& name = getFsName();
	const fs::path& pwd = myMenu->pwd();
	bool back = name.compare("..") == 0;

	fs::path path = back ? pwd.parent_path() : pwd / name;
	std::error_code error;
	bool is_file = fs::is_regular_file(path, error);
	if (error) {
		myMenu->showError("File unavailable. Press B to go back.");
		return;
	}

	if (!back && is_file) {
		const std::string& ext = path.extension();

		if (ext.empty()) {
			myMenu->prepareCredits(path);
		} else if (is_cd_image_ext(ext)) {
			myMenu->requestLoad(path.c_str(), "Checking disc image...");
		} else {
			myMenu->showError("Not a disc image. Press B to go back.");
		}
	} else {
		myMenu->preparePopulate(path, back, false);
	}
}

void PathLabel::cancel()
{
	fs::path path = myMenu->pwd().parent_path();
	bool to_menu = path == myMenu->pwd() || path == "/rd";

	myMenu->preparePopulate(path, true, to_menu);
}

void MainMenuLabel::cancel()
{
	/* No action for cancel on the main menu */
}

void TextLabel::activate()
{
	/* No action for activate on regular text */
}

void TextLabel::cancel()
{
	myMenu->preparePopulate("/rd/credits", true, false);
}

ToggleLabel::ToggleLabel(std::shared_ptr<Font> fh, enum bloom_setting_id id, int size)
	: MyLabel(fh, "", size, false,
		  Color(1.0f, 1.0f, 0.95f, 0.7f),
		  Color(1.0f, 0.7f, 0.7f, 0.7f)),
	  m_id(id)
{
	char buf[80];

	bloom_settings_line(id, buf, sizeof(buf));
	m_label = buf;
	setText(m_label);
}

void ToggleLabel::activate()
{
	char buf[80];

	if (!bloom_settings_cycle(m_id)) {
		myMenu->showError("This build cannot change that option");
		return;
	}

	bloom_settings_line(m_id, buf, sizeof(buf));
	m_label = buf;
	setText(m_label);
	input_apply_settings();
	if (bloom_settings_save() == 0) {
		const char *path = bloom_settings_path();
		myMenu->showStatus(path[0] ? (std::string("Saved ") + path)
					   : "Settings saved");
	} else {
		myMenu->showError("Could not save settings");
	}
}

void ToggleLabel::cancel()
{
	myMenu->persistBrowsePath();
	myMenu->preparePopulate(fs::path(TOP_PATH), true, true);
}

void InfoLabel::activate()
{
	/* No action for activate on info text */
}

void InfoLabel::cancel()
{
	myMenu->preparePopulate(fs::path(TOP_PATH), true, true);
}

MyMenu::MyMenu(std::shared_ptr<Font> fnt, const fs::path &path)
	: m_path(path), m_cursel(0), m_font_size(MENU_ENTRY_SIZE),
	  m_xoffset(SCREEN_W / 2), m_list_y(MAIN_LIST_Y)
{
	m_bg = std::make_shared<Background>();

	m_bg->setTint(Color(1.0f, 0.55f, 0.55f, 0.6f));

	m_top_scene = std::make_shared<Scene>();
	m_scene->subAdd(m_bg);
	m_scene->subAdd(m_top_scene);

	m_top_scene->setTranslate(Vector(-static_cast<float>(m_xoffset), m_list_y, 10));

	m_color0 = Color(1, 1, 1, 1);
	m_color1 = Color(1, 0.5f, 0.5f, 0.5f);
	m_input_allowed = false;
	m_pending_load = false;
	m_wrap = true;

	m_font = fnt;
	m_exited = false;

	m_title = std::make_shared<Label>(m_font, "Bloom", 28, true, true);
	m_title->setTranslate(Vector(SCREEN_W / 2, TITLE_Y, 30));
	m_title->setTint(Color(1.0f, 1.0f, 0.92f, 0.78f));
	m_scene->subAdd(m_title);

	m_location = std::make_shared<Label>(m_font, "", 16, true, true);
	m_location->setTranslate(Vector(SCREEN_W / 2, SUBTITLE_Y, 30));
	m_location->setTint(Color(1.0f, 0.82f, 0.82f, 0.82f));
	m_scene->subAdd(m_location);

	m_status = std::make_shared<Label>(m_font, "", 16, true, true);
	m_status->setTranslate(Vector(SCREEN_W / 2, STATUS_Y, 30));
	m_status->setTint(Color(1.0f, 1.0f, 0.4f, 0.4f));
	m_scene->subAdd(m_status);

	m_hint = std::make_shared<Label>(m_font, "", 14, true, true);
	m_hint->setTranslate(Vector(SCREEN_W / 2, HINT_Y, 30));
	m_hint->setTint(Color(1.0f, 0.7f, 0.7f, 0.7f));
	m_scene->subAdd(m_hint);

	setAutoRepeat(Event::KeyUp, true);
	setAutoRepeat(Event::KeyDown, true);
	setAutoRepeat(Event::KeyLeft, true);
	setAutoRepeat(Event::KeyRight, true);
	setAutoRepeat(Event::KeyPgup, true);
	setAutoRepeat(Event::KeyPgdn, true);
	setTimeout(3600);

	populate_dft();
}

void MyMenu::setChrome(const std::string &location, const std::string &hint)
{
	if (m_location)
		m_location->setText(location);
	if (m_hint)
		m_hint->setText(hint);
}

void MyMenu::addEntry(std::shared_ptr<MyLabel> entry)
{
	entry->setTranslate(Vector(0, m_font_size * m_entries.size(), 0));
	m_top_scene->subAdd(entry);

	if (m_entries.empty())
		entry->select();

	m_entries.push_back(entry);
}

void MyMenu::populate_dft()
{
	m_font_size = MENU_ENTRY_SIZE;
	m_xoffset = SCREEN_W / 2;
	m_list_y = MAIN_LIST_Y;
	m_wrap = true;

	std::shared_ptr<AnimFadeIn> anim;

	m_entries.clear();
	m_top_scene->animRemoveAll();
	m_top_scene->subRemoveAll();
	m_top_scene->setTranslate(Vector(800.0f, m_list_y, 10));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Run CD-ROM", m_font_size,
						 [&] {
		requestLoad(nullptr, "Checking CD-ROM...");
	}));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Select CD image", m_font_size,
						 [&] {
		preparePopulate(usable_browse_path(last_browse), false, false);
	}));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Settings", m_font_size,
						 [&] {
		prepareSettings();
	}));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Build info", m_font_size,
						 [&] {
		prepareOptions();
	}));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Credits", m_font_size,
						 [&] {
		preparePopulate("/rd/credits", false, false);
	}));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Quit", m_font_size,
						 [&] {
		this->m_exited = true;
		startExit();
	}));

	anim = std::make_shared<AnimFadeIn>(false, m_xoffset, [&] {
		m_top_scene->animRemoveAll();
	});
	m_top_scene->animRemoveAll();
	m_top_scene->animAdd(anim);

	m_input_allowed = true;
	m_cursel = 0;
	m_path = TOP_PATH;
	clearError();
	setChrome("PlayStation emulator", "A Select");
	bloom_settings_flush();
}

void MyMenu::populate(fs::path path, bool back, const std::string &select_name)
{
	float dx = back ? 1.0f : -1.0f;
	std::shared_ptr<AnimFadeIn> anim;
	std::set<fs::path> fileset;
	std::set<fs::path> dirset;
	const dirent_t *d;
	bool is_file;
	bool is_credits = path.compare("/rd/credits") == 0;
	int fd;
	bool open_failed = false;
	char loc[96];

	m_font_size = ENTRY_SIZE;
	m_xoffset = 48;
	m_list_y = LIST_Y;

	m_entries.clear();
	m_top_scene->animRemoveAll();
	m_top_scene->subRemoveAll();
	m_top_scene->setTranslate(Vector(dx * -800.0f, m_list_y, 10));

	fd = fs_open(path.c_str(), O_DIR);
	if (fd == -1) {
		open_failed = true;
		fprintf(stderr, "Unable to open directory: %s\n", path.c_str());
		if (!m_path.empty() && m_path != path) {
			fd = fs_open(m_path.c_str(), O_DIR);
			if (fd != -1)
				path = m_path;
		}
		if (fd == -1 && last_browse != path) {
			fd = fs_open(last_browse.c_str(), O_DIR);
			if (fd != -1)
				path = last_browse;
		}
		if (fd == -1) {
			populate_dft();
			showError("Unable to open directory");
			return;
		}
	}
	is_credits = path == "/rd/credits";
	m_wrap = !is_credits;

	while ((d = fs_readdir(fd))) {
		std::string name = d->name;
		fs::path filepath = name;
		std::error_code error;
		is_file = fs::is_regular_file(path / filepath, error);
		if (error)
			continue;

		if (name == "." || name == "..")
			continue;
		if (!name.empty() && name[0] == '.')
			continue;

		if (is_file) {
			const std::string& ext = filepath.extension();

			if (!is_cd_image_ext(ext) && (!is_credits || !ext.empty()))
				continue;
		} else if (path == TOP_PATH) {
			if (!menu_is_browser_root(name.c_str()))
				continue;
		}

		if (is_file)
			fileset.insert(filepath);
		else
			dirset.insert(filepath);
	}

	for (fs::path filepath : dirset) {
		std::string fs_name = filepath.string();
		std::string display = (path == TOP_PATH)
			? menu_volume_label(fs_name.c_str())
			: fs_name + "/";

		addEntry(std::make_shared<PathLabel>(m_font, trunc_label(display, 38),
						     false, m_font_size, fs_name));
	}

	for (fs::path filepath : fileset) {
		std::string fs_name = filepath.string();

		addEntry(std::make_shared<PathLabel>(m_font, trunc_label(fs_name, 38),
						     true, m_font_size, fs_name));
	}

	anim = std::make_shared<AnimFadeIn>(false, m_xoffset, [&] {
		m_top_scene->animRemoveAll();
	});
	m_top_scene->animRemoveAll();
	m_top_scene->animAdd(anim);
	fs_close(fd);

	m_path = path;
	if (!is_credits && path != "/rd") {
		last_browse = path;
		bloom_settings_set_last_path(path.c_str());
	}
	m_cursel = 0;
	m_input_allowed = true;
	clearError();

	if (!select_name.empty()) {
		for (unsigned int i = 0; i < m_entries.size(); i++) {
			if (m_entries[i]->getFsName() == select_name) {
				setEntry(i);
				break;
			}
		}
	}

	if (is_credits) {
		setChrome("Credits", "A Open   B Back   D-pad Scroll");
	} else {
		menu_format_location(loc, sizeof(loc), path.c_str());
		setChrome(trunc_label(loc, 48), "A Open   B Back   L/R Page");
	}

	if (open_failed)
		showError("Unable to open folder; returned to previous folder");
	else if (m_entries.empty()) {
		if (is_credits)
			showError("No credits found. Press B to go back.");
		else if (path == TOP_PATH)
			showError("No devices found. Press B to go back.");
		else
			showError("No disc images found. Press B to go back.");
	}
}

void MyMenu::preparePopulate(fs::path path, bool back, bool dft)
{
	float dx = back ? 1.0f : -1.0f;
	std::string restore;

	if (back && !m_path.empty() && m_path != TOP_PATH)
		restore = m_path.filename().string();

	std::error_code error;
	if (back || fs::is_directory(path, error) || path == fs::path(TOP_PATH)) {
		auto anim = std::make_shared<AnimFadeAway>(false, dx,
							   800.0f * dx, [=, this] {
			if (dft)
				populate_dft();
			else
				populate(path, back, restore);
		});

		m_top_scene->animRemoveAll();
		m_top_scene->animAdd(anim);
		m_input_allowed = false;
	} else {
		showError("Folder unavailable. Press B to go back.");
	}
}

void MyMenu::prepareCredits(fs::path path)
{
	auto anim = std::make_shared<AnimFadeAway>(false, -1.0f,
						   -800.0f, [=, this] {
		populateCredits(path);
	});

	m_top_scene->animRemoveAll();
	m_top_scene->animAdd(anim);
	m_input_allowed = false;
}

void MyMenu::populateCredits(fs::path path)
{
	std::ifstream fd(path);
	std::shared_ptr<AnimFadeIn> anim;
	std::string line;

	if (!fd.is_open()) {
		populate("/rd/credits", true);
		showError("Unable to open credits. Press B to go back.");
		return;
	}

	m_font_size = CREDITS_ENTRY_SIZE;
	m_xoffset = 36;
	m_list_y = LIST_Y;
	m_wrap = false;

	m_entries.clear();
	m_top_scene->animRemoveAll();
	m_top_scene->subRemoveAll();
	m_top_scene->setTranslate(Vector(800.0f, m_list_y, 10));

	while (std::getline(fd, line)) {
		addEntry(std::make_shared<TextLabel>(m_font, trunc_label(line, 72),
						     CREDITS_ENTRY_SIZE));
	}
	if (m_entries.empty())
		addEntry(std::make_shared<TextLabel>(m_font,
			"Credits file is empty. Press B to go back.", m_font_size));

	anim = std::make_shared<AnimFadeIn>(false, m_xoffset, [&] {
		m_top_scene->animRemoveAll();
	});
	m_top_scene->animAdd(anim);

	m_input_allowed = true;
	m_cursel = 0;
	clearError();
	setChrome(trunc_label(path.filename().string(), 40),
		  "B Back   D-pad Scroll");
}

void MyMenu::prepareOptions()
{
	auto anim = std::make_shared<AnimFadeAway>(false, -1.0f,
						   -800.0f, [=, this] {
		populateOptions();
	});

	m_top_scene->animRemoveAll();
	m_top_scene->animAdd(anim);
	m_input_allowed = false;
}

void MyMenu::populateOptions()
{
	std::shared_ptr<AnimFadeIn> anim;
	auto add_info = [&](const std::string &line) {
		addEntry(std::make_shared<InfoLabel>(m_font, line, CREDITS_ENTRY_SIZE));
	};

	m_font_size = CREDITS_ENTRY_SIZE;
	m_xoffset = 36;
	m_list_y = LIST_Y;
	m_wrap = false;

	m_entries.clear();
	m_top_scene->animRemoveAll();
	m_top_scene->subRemoveAll();
	m_top_scene->setTranslate(Vector(800.0f, m_list_y, 10));

	add_info(std::string("Build  ") + REV);
	add_info("Compile-time features. Live toggles are in Settings.");
	add_info("");
	add_info(std::string("GPU   ") + GPU_PLUGIN +
		 (WITH_PVR_SOFTWARE ? "  — software diagnostic mode"
		  : HARDWARE_ACCELERATED ? "  — faster, lower compatibility"
				       : "  — slower, higher compatibility"));
	add_info(std::string("SPU   ") + SPU_PLUGIN +
		 (std::string(SPU_PLUGIN) == "AICA"
		  ? "  — dfsound mix, AICA output"
		  : "  — silent, SPU IRQs emulated"));
	add_info(std::string("24-bit framebuffer   ") + (WITH_24BPP ? "on" : "off")
		 + "  (rebuild to change)");
	add_info(std::string("CHD images   ") + (WITH_CHD ? "on" : "off"));
	add_info(std::string("IDE   ") + (WITH_IDE ? "on" : "off") +
		 "     SD   " + (WITH_SDCARD ? "on" : "off"));
	add_info("");
	add_info("Current Settings (rumble and analog apply immediately)");
	{
		char line[80];
		unsigned int id;

		for (id = 0; id < BLOOM_SET_COUNT; id++) {
			bloom_settings_line((enum bloom_setting_id)id, line,
					    sizeof(line));
			add_info(line);
		}
	}
	add_info("");
	add_info("Controls");
	add_info("A Cross          START+A Select");
	add_info("B Circle         START+B R3");
	add_info("X Square         START+X L3");
	add_info("Y Triangle       Z Select");
	add_info("C L2   D R2      L/R triggers  L1/R1");
	add_info("START  Start (hold with another button for combos)");
	add_info("START + analog   right stick");
	add_info("START+A+B+X+Y    quit emulator");
	add_info("START+D-pad Up   screenshot to /pc");
	add_info("");
	add_info("GPU, SPU, 24-bit, CHD, IDE, and SD need a rebuild. Press B to go back.");

	anim = std::make_shared<AnimFadeIn>(false, m_xoffset, [&] {
		m_top_scene->animRemoveAll();
	});
	m_top_scene->animAdd(anim);

	m_input_allowed = true;
	m_cursel = 0;
	clearError();
	setChrome("Build info", "B Back   D-pad Scroll");
}

void MyMenu::persistBrowsePath()
{
	std::string path = last_browse.string();

	if (!menu_path_allowed(path.c_str()))
		return;
	bloom_settings_set_last_path(path.c_str());
	bloom_settings_flush();
}

void MyMenu::prepareSettings()
{
	auto anim = std::make_shared<AnimFadeAway>(false, -1.0f,
						   -800.0f, [=, this] {
		populateSettings();
	});

	m_top_scene->animRemoveAll();
	m_top_scene->animAdd(anim);
	m_input_allowed = false;
}

void MyMenu::populateSettings()
{
	std::shared_ptr<AnimFadeIn> anim;
	const char *cfg = bloom_settings_path();
	auto add_info = [&](const std::string &line) {
		addEntry(std::make_shared<InfoLabel>(m_font, line, CREDITS_ENTRY_SIZE));
	};
	struct bloom_settings *opt = bloom_settings_get();

	m_font_size = CREDITS_ENTRY_SIZE;
	m_xoffset = 36;
	m_list_y = LIST_Y;
	m_wrap = false;

	m_entries.clear();
	m_top_scene->animRemoveAll();
	m_top_scene->subRemoveAll();
	m_top_scene->setTranslate(Vector(800.0f, m_list_y, 10));

	auto add_toggle = [&](enum bloom_setting_id id, bool allowed) {
		if (allowed)
			addEntry(std::make_shared<ToggleLabel>(m_font, id,
							       CREDITS_ENTRY_SIZE));
		else {
			char line[80];

			bloom_settings_line(id, line, sizeof(line));
			add_info(line);
		}
	};

	add_toggle(BLOOM_SET_SILENT_AUDIO, opt && opt->allow_aica);
	addEntry(std::make_shared<ToggleLabel>(m_font, BLOOM_SET_RUMBLE,
					       CREDITS_ENTRY_SIZE));
	addEntry(std::make_shared<ToggleLabel>(m_font, BLOOM_SET_ANALOG,
					       CREDITS_ENTRY_SIZE));
	add_toggle(BLOOM_SET_VIDEO_480P, opt && opt->allow_480p);
	if (opt && opt->allow_bilinear)
		addEntry(std::make_shared<ToggleLabel>(m_font, BLOOM_SET_BILINEAR,
						       CREDITS_ENTRY_SIZE));
	if (opt && opt->allow_hybrid)
		addEntry(std::make_shared<ToggleLabel>(m_font, BLOOM_SET_HYBRID,
						       CREDITS_ENTRY_SIZE));
	if (opt && opt->allow_clipping)
		addEntry(std::make_shared<ToggleLabel>(m_font, BLOOM_SET_CLIPPING,
						       CREDITS_ENTRY_SIZE));
	if (opt && opt->allow_fsaa)
		addEntry(std::make_shared<ToggleLabel>(m_font, BLOOM_SET_FSAA,
						       CREDITS_ENTRY_SIZE));
	add_info("");
	add_info(cfg[0] ? (std::string("Saved at  ") + cfg)
			: "Saved to /sd, /ide, or /ram when possible");
	add_info("A toggles. Analog and rumble apply now.");
	add_info("Video, audio, bilinear, hybrid, clip, FSAA: next launch.");
	add_info("");
	add_info(std::string("GPU plugin     ") + GPU_PLUGIN +
		 "  (rebuild to change)");
	add_info(std::string("SPU mix        ") + SPU_PLUGIN +
		 "  (rebuild to change)");
	add_info(std::string("24-bit FB      ") + (WITH_24BPP ? "on" : "off") +
		 "  (rebuild to change)");
	add_info("CHD, IDE, and SD stay compile-time.");

	anim = std::make_shared<AnimFadeIn>(false, m_xoffset, [&] {
		m_top_scene->animRemoveAll();
	});
	m_top_scene->animAdd(anim);

	m_input_allowed = true;
	m_cursel = 0;
	clearError();
	setChrome("Settings", "A Toggle   B Back");
}

void MyMenu::showError(const std::string &msg)
{
	if (m_status) {
		m_status->setText(msg);
		m_status->setTint(Color(1.0f, 1.0f, 0.4f, 0.4f));
	}
}

void MyMenu::showStatus(const std::string &msg)
{
	if (m_status) {
		m_status->setText(msg);
		m_status->setTint(Color(1.0f, 1.0f, 0.85f, 0.45f));
	}
}

void MyMenu::clearError()
{
	if (m_status) {
		m_status->setText("");
		m_status->setTint(Color(1.0f, 1.0f, 0.85f, 0.45f));
	}
}

void MyMenu::requestLoad(const char *path, const char *busy_msg)
{
	showStatus(busy_msg ? busy_msg : "Checking disc...");
	m_pending_iso = path ? path : "";
	m_pending_load = true;
	m_input_allowed = false;
}

void MyMenu::visualPerFrame()
{
	GenericMenu::visualPerFrame();

	if (!m_pending_load)
		return;

	m_pending_load = false;

	if (emu_check_cd(m_pending_iso.empty() ? nullptr : m_pending_iso.c_str())) {
		extern char CdromId[10];
		if (CdromId[0])
			showStatus(std::string("Starting ") + CdromId);
		else
			clearError();
		persistBrowsePath();
		startExit();
	} else {
		showError(emu_last_cd_error());
		m_input_allowed = true;
	}
}

unsigned int MyMenu::pageStep() const
{
	return menu_page_step(m_list_y, LIST_BOTTOM, m_font_size);
}

void MyMenu::moveSelection(int delta, bool wrap)
{
	int next;

	if (m_entries.empty())
		return;

	next = (int)m_cursel + delta;
	if (wrap)
		setEntry(menu_wrap_index(next, m_entries.size()));
	else
		setEntry(menu_clamp_index(next, m_entries.size()));
}

void MyMenu::setEntry(unsigned int entry) {
	int offset_y;
	if (entry >= m_entries.size())
		return;

	m_entries[m_cursel]->deselect();
	m_cursel = entry;

	offset_y = (int)m_list_y + (int)entry * -(int)m_font_size;

	m_entries[entry]->select();
	m_top_scene->animRemoveAll();
	m_top_scene->animAdd(std::make_shared<LogXYMover>(m_xoffset, offset_y));
}

void MyMenu::inputEvent(const Event & evt) {
	if(evt.type != Event::EvtKeypress)
		return;

	if (!m_input_allowed)
		return;

	if (m_entries.empty()) {
		if (evt.key == Event::KeyCancel)
			preparePopulate(pwd().parent_path(), true,
					pwd().parent_path() == pwd() ||
					pwd().parent_path() == "/rd");
		return;
	}

	switch(evt.key) {
	case Event::KeyUp:
		moveSelection(-1, m_wrap);
		break;

	case Event::KeyLeft:
	case Event::KeyPgup:
		moveSelection(-(int)pageStep(), false);
		break;

	case Event::KeyDown:
		moveSelection(1, m_wrap);
		break;
	case Event::KeyRight:
	case Event::KeyPgdn:
		moveSelection((int)pageStep(), false);
		break;
	case Event::KeyCancel:
		m_entries[m_cursel]->cancel();

		break;
	case Event::KeySelect:
		m_entries[m_cursel]->activate();

		break;
	case Event::KeyStart:
	case Event::KeyMiscX:
	case Event::KeyMiscY:
	case Event::KeyReset:
		break;
	default:
		break;
	}
}

void MyMenu::startExit() {
	m_input_allowed = false;
	m_pending_load = false;
	persistBrowsePath();
	// Apply some expmovers to the options.

	for (unsigned int i = 0; i < m_entries.size(); i++) {
		auto m = std::make_shared<ExpXYMover>(0, 1.0f + 0.2f * (float)i, 0, 1200);
		m->triggerAdd(std::make_shared<Death>());
		m_entries[i]->animAdd(m);
	}

	auto f = std::make_shared<AlphaFader>(0.0f, -1.0f / 60.0f);
	m_bg->animAdd(f);

	GenericMenu::startExit();
}

void AnimFadeAway::nextFrame(Drawable *t) {
	Vector p = t->getTranslate();
	float delta_x, delta_y, max_x, max_y, value = m_vertical ? p.y : p.x;
	bool done = m_delta < 0 ? (value <= m_max) : (value >= m_max);

	if (m_vertical) {
		delta_x = 0.0f;
		delta_y = m_delta;
		max_x = p.x;
		max_y = m_max;
	} else {
		delta_x = m_delta;
		delta_y = 0.0f;
		max_x = m_max;
		max_y = p.y;
	}

	if (done) {
		t->setTranslate(Vector(max_x, max_y, p.z));
		complete(t);
		return;
	}

	// Move 1.15x of the distance each frame
	p += Vector(delta_x, delta_y, 0);
	t->setTranslate(p);
	m_delta *= 1.15f;
}

void AnimFadeIn::nextFrame(Drawable *t) {
	float delta, delta_x, delta_y, max_x, max_y;
	Vector p = t->getTranslate();

	if (m_vertical) {
		max_x = p.x;
		max_y = m_max;
		delta = m_max - p.y;
		delta_x = 0.0f;
		delta_y = delta;
	} else {
		max_x = m_max;
		max_y = p.y;
		delta = m_max - p.x;
		delta_x = delta;
		delta_y = 0.0f;
	}

	if (fabs(delta) < 1.0f) {
		t->setTranslate(Vector(max_x, max_y, p.z));
		complete(t);
	} else {
		// Move 1/8th of the distance each frame
		p += Vector(delta_x / 8.0f, delta_y / 8.0f, 0);

		t->setTranslate(p);
	}
}

extern "C" bool runMenu(void)
{
	bool exited;
	const char *saved = bloom_settings_get()->last_path;
	const char *err;

	last_browse = usable_browse_path(saved);

	// Load a font
	auto fnt = std::make_shared<Font>("/rd/typewriter.txf");

	// Create a menu
	myMenu = std::make_shared<MyMenu>(fnt, fs::path(TOP_PATH));

	err = emu_last_cd_error();
	if (err && err[0])
		myMenu->showError(err);

	// Do the menu
	myMenu->doMenu();

	exited = myMenu->hasExited();

	// Ok, we're all done! The RefPtrs will take care of mem cleanup.
	myMenu = nullptr;

	return exited;
}
