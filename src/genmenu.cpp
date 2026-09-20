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
#include <functional>
#include <cctype>
#include <set>
#include <string>
#include <vector>

#include "background.h"
#include "emu.h"
#include "genmenu.h"
#include "bloom-config.h"

#define MENU_OFF_X 200
#define MENU_OFF_Y 200

#define MENU_ENTRY_SIZE 32
#define ENTRY_SIZE 20
#define CREDITS_ENTRY_SIZE 11

#define TOP_PATH "/"

static std::string ascii_lower(std::string s)
{
	for (char &c : s)
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return s;
}

static bool is_cd_image_ext(const std::string &ext)
{
	const std::string e = ascii_lower(ext);

	return e == ".iso" || e == ".cue" || e == ".ccd" || e == ".exe"
		|| e == ".mds" || e == ".pbp" || e == ".bin" || e == ".img"
		|| e == ".mdf" || (WITH_CHD && e == ".chd");
}

static std::shared_ptr<MyMenu> myMenu;

MyLabel::MyLabel(std::shared_ptr<Font> fh, const std::string& text, int size,
		 bool centered, const Color& selected, const Color& deselected) :
	Label(fh, "", size, centered, false),
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
	const std::string& name = getLabel();
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
		} else if (emu_check_cd(path.c_str())) {
			/* Launch ISO! */
			myMenu->clearError();
			myMenu->startExit();
		} else {
			myMenu->showError("Could not load this image");
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
	  m_xoffset(MENU_OFF_X)
{
	m_bg = std::make_shared<Background>();

	m_bg->setTint(Color(1.0f, 0.7f, 0.7f, 0.7f));

	m_top_scene = std::make_shared<Scene>();
	m_scene->subAdd(m_bg);
	m_scene->subAdd(m_top_scene);

	m_top_scene->setTranslate(Vector(-static_cast<float>(m_xoffset), MENU_OFF_Y, 10));

	m_color0 = Color(1, 1, 1, 1);
	m_color1 = Color(1, 0.5f, 0.5f, 0.5f);
	m_input_allowed = false;

	m_font = fnt;
	m_exited = false;

	m_status = std::make_shared<Label>(m_font, "", 18, true, false);
	m_status->setTranslate(Vector(320, 440, 20));
	m_status->setTint(Color(1.0f, 1.0f, 0.35f, 0.35f));
	m_scene->subAdd(m_status);

	populate_dft();
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
	m_xoffset = MENU_OFF_X;

	std::shared_ptr<AnimFadeIn> anim;

	m_entries.clear();
	m_top_scene->animRemoveAll();
	m_top_scene->subRemoveAll();
	m_top_scene->setTranslate(Vector(800.0f, MENU_OFF_Y, 10));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Run CD-ROM", m_font_size,
						 [&] {
		if (emu_check_cd(nullptr)) {
			/* Launch CD-Rom! */
			clearError();
			startExit();
		} else {
			showError("No PlayStation disc detected");
		}
	}));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Select CD image", m_font_size,
						 [&] {
		preparePopulate(fs::path(TOP_PATH), false, false);
	}));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Build info", m_font_size,
						 [&] {
		prepareOptions();
	}));

	addEntry(std::make_shared<MainMenuLabel>(m_font, "Credits", m_font_size,
						 [&] {
		myMenu->preparePopulate("/rd/credits", false, false);
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
	clearError();
}

void MyMenu::populate(fs::path path, bool back)
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

	m_font_size = ENTRY_SIZE;
	m_xoffset = 200;

	m_entries.clear();
	m_top_scene->animRemoveAll();
	m_top_scene->subRemoveAll();
	m_top_scene->setTranslate(Vector(dx * -800.0f, MENU_OFF_Y, 10));

	fd = fs_open(path.c_str(), O_DIR);
	if (fd == -1) {
		open_failed = true;
		fprintf(stderr, "Unable to open directory: %s\n", path.c_str());
		if (!m_path.empty() && m_path != path)
			fd = fs_open(m_path.c_str(), O_DIR);
		if (fd == -1) {
			populate_dft();
			showError("Unable to open directory");
			return;
		}
		path = m_path;
	}
	is_credits = path == "/rd/credits";

	while ((d = fs_readdir(fd))) {
		std::string name = d->name;
		fs::path filepath = name;
		std::error_code error;
		is_file = fs::is_regular_file(path / filepath, error);
		if (error)
			continue;

		if (name == ".")
			continue;

		if (is_file) {
			const std::string& ext = filepath.extension();

			if (!is_cd_image_ext(ext) && (!is_credits || !ext.empty()))
				continue;
		} else if (path == TOP_PATH) {
			if (name != "cd"
			    && name != "pc"
			    && name != "ide"
			    && name != "sd") {
				continue;
			}
		} else if (name == "..") {
			continue;
		}

		if (is_file)
			fileset.insert(filepath);
		else
			dirset.insert(filepath);
	}

	for (fs::path filepath : dirset) {
		addEntry(std::make_shared<PathLabel>(m_font, filepath,
						     false, m_font_size));
	}

	for (fs::path filepath : fileset) {
		addEntry(std::make_shared<PathLabel>(m_font, filepath,
						     true, m_font_size));
	}

	anim = std::make_shared<AnimFadeIn>(false, m_xoffset, [&] {
		m_top_scene->animRemoveAll();
	});
	m_top_scene->animRemoveAll();
	m_top_scene->animAdd(anim);
	fs_close(fd);

	m_path = path;
	m_cursel = 0;
	m_input_allowed = true;
	clearError();
	if (open_failed)
		showError("Unable to open folder; returned to previous folder");
	else if (m_entries.empty())
		showError(is_credits ? "No credits found. Press B to go back."
			  : "No disc images found. Press B to go back.");
}

void MyMenu::preparePopulate(fs::path path, bool back, bool dft)
{
	float dx = back ? 1.0f : -1.0f;

	std::error_code error;
	if (back || fs::is_directory(path, error) || path == fs::path(TOP_PATH)) {
		auto anim = std::make_shared<AnimFadeAway>(false, dx,
							   800.0f * dx, [=, this] {
			if (dft)
				populate_dft();
			else
				populate(path, back);
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
	m_xoffset = 10;

	m_entries.clear();
	m_top_scene->animRemoveAll();
	m_top_scene->subRemoveAll();
	m_top_scene->setTranslate(Vector(800.0f, MENU_OFF_Y, 10));

	while (std::getline(fd, line)) {
		addEntry(std::make_shared<TextLabel>(m_font, line,
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
	m_xoffset = 10;

	m_entries.clear();
	m_top_scene->animRemoveAll();
	m_top_scene->subRemoveAll();
	m_top_scene->setTranslate(Vector(800.0f, MENU_OFF_Y, 10));

	add_info("Build options (compile-time)");
	add_info("");
	add_info(std::string("GPU: ") + GPU_PLUGIN +
		 (HARDWARE_ACCELERATED ? " (faster, lower compatibility)"
				       : " (slower, higher compatibility)"));
	add_info(std::string("SPU: ") + SPU_PLUGIN +
		 (std::string(SPU_PLUGIN) == "AICA"
		  ? " (dfsound mix, AICA output)"
		  : " (silent, SPU IRQs emulated)"));
	add_info(std::string("Resolution: ") + (WITH_480P ? "640x480" : "320x240"));
	add_info(std::string("Hybrid rendering: ") + (WITH_HYBRID_RENDERING ? "on" : "off"));
	add_info(std::string("FSAA: ") + (WITH_FSAA ? "on" : "off"));
	add_info(std::string("24-bit framebuffer: ") + (WITH_24BPP ? "on" : "off"));
	add_info(std::string("Bilinear filtering: ") + (WITH_BILINEAR ? "on" : "off"));
	add_info(std::string("Pixel clipping: ") + (WITH_CLIPPING ? "on" : "off"));
	add_info(std::string("CHD images: ") + (WITH_CHD ? "on" : "off"));
	add_info(std::string("IDE: ") + (WITH_IDE ? "on" : "off") +
		 "   SD: " + (WITH_SDCARD ? "on" : "off"));
	add_info("");
	add_info("Controls");
	add_info("A Cross     START+A Select");
	add_info("B Circle    START+B R3");
	add_info("X Square    START+X L3");
	add_info("Y Triangle  Z Select  C L2  D R2");
	add_info("L/R triggers  L1/R1    START+L/R  L2/R2");
	add_info("START  Start (hold with another button for combos)");
	add_info("START + analog stick  right stick");
	add_info("START+A+B+X+Y  quit emulator");
	add_info("START+D-pad Up  screenshot to /pc");
	add_info("");
	add_info("Change these with kos-ccmake. Press B to go back.");

	anim = std::make_shared<AnimFadeIn>(false, m_xoffset, [&] {
		m_top_scene->animRemoveAll();
	});
	m_top_scene->animAdd(anim);

	m_input_allowed = true;
	m_cursel = 0;
	clearError();
}

void MyMenu::showError(const std::string &msg)
{
	if (m_status) {
		m_status->setText(msg);
		m_status->setTint(Color(1.0f, 1.0f, 0.35f, 0.35f));
	}
}

void MyMenu::clearError()
{
	if (m_status)
		m_status->setText("");
}

void MyMenu::setEntry(unsigned int entry) {
	int offset_y;
	if (entry >= m_entries.size())
		return;

	m_entries[m_cursel]->deselect();
	m_cursel = entry;

	offset_y = MENU_OFF_Y + (int)entry * -(int)m_font_size;

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
		if (m_cursel > 0)
			setEntry(m_cursel - 1);
		break;

	case Event::KeyLeft:
		if (m_cursel > 0)
			setEntry(m_cursel > 5 ? m_cursel - 5 : 0);
		break;

	case Event::KeyDown:
		if (m_cursel + 1 < m_entries.size())
			setEntry(m_cursel + 1);

		break;
	case Event::KeyRight:
		if (m_cursel + 1 < m_entries.size()) {
			unsigned int entry;

			if (m_cursel + 5 < m_entries.size())
				entry = m_cursel + 5;
			else
				entry = m_entries.size() - 1;


			setEntry(entry);
		}
		break;
	case Event::KeyCancel:
		m_entries[m_cursel]->cancel();

		break;
	case Event::KeySelect:
		m_entries[m_cursel]->activate();

		break;
	default:
		printf("Unhandled Event Key\n");
		break;
	}
}

void MyMenu::startExit() {
	m_input_allowed = false;
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

	// Load a font
	auto fnt = std::make_shared<Font>("/rd/typewriter.txf");

	// Create a menu
	myMenu = std::make_shared<MyMenu>(fnt, fs::path(TOP_PATH));

	// Do the menu
	myMenu->doMenu();

	exited = myMenu->hasExited();

	// Ok, we're all done! The RefPtrs will take care of mem cleanup.
	myMenu = nullptr;

	return exited;
}
