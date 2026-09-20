/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Emulator's GUI
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#ifndef GENMENU_H
#define GENMENU_H

#include <tsu/drawables/label.h>
#include <tsu/font.h>

#include <functional>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "settings.h"

namespace fs = std::filesystem;

typedef std::function<void(void)> Action;

class Background;

class MyLabel : public Label {
public:
	MyLabel(std::shared_ptr<Font> fh, const std::string &text, int size,
		bool centered, const Color& selected, const Color& deselected);

	~MyLabel() {}

	const std::string &getLabel() { return m_label; }
	const std::string &getFsName() { return m_fs_name.empty() ? m_label : m_fs_name; }

	void setFsName(const std::string &name) { m_fs_name = name; }

	void select()
	{
		setTint(m_color_selected);
	}

	void deselect()
	{
		setTint(m_color_deselected);
	}

	unsigned int fontSize() const {
		return m_size;
	}

	virtual void activate() = 0;
	virtual void cancel() = 0;

	void draw(int list);

protected:
	Color m_color_selected, m_color_deselected;
	std::string m_label;
	std::string m_fs_name;
	std::shared_ptr<Font> m_font;
	unsigned int m_size;
};

class PathLabel : public MyLabel {
public:
	PathLabel(std::shared_ptr<Font> fh, const std::string& text, bool is_file, int size,
		  const std::string& fs_name = std::string())
		: MyLabel(fh, text, size, false,
			  is_file ? Color(1.0f, 0.85f, 0.9f, 1.0f) : Color(1.0f, 1.0f, 1.0f, 1.0f),
			  is_file ? Color(1.0f, 0.45f, 0.55f, 0.95f) : Color(1.0f, 0.62f, 0.62f, 0.62f))
	{
		setFsName(fs_name.empty() ? text : fs_name);
	}

	~PathLabel() {}

	virtual void activate();
	virtual void cancel();
};

class TextLabel : public MyLabel {
public:
	TextLabel(std::shared_ptr<Font> fh, const std::string& text, int size)
		: MyLabel(fh, text, size, false,
			  Color(1.0f, 1.0f, 1.0f, 1.0f),
			  Color(1.0f, 0.85f, 0.85f, 0.85f))
	{
	}

	~TextLabel() {}

	virtual void activate();
	virtual void cancel();
};

class InfoLabel : public MyLabel {
public:
	InfoLabel(std::shared_ptr<Font> fh, const std::string& text, int size)
		: MyLabel(fh, text, size, false,
			  Color(1.0f, 1.0f, 1.0f, 1.0f),
			  Color(1.0f, 0.85f, 0.85f, 0.85f))
	{
	}

	~InfoLabel() {}

	virtual void activate();
	virtual void cancel();
};

class MainMenuLabel : public MyLabel {
public:
	MainMenuLabel(std::shared_ptr<Font> fh, const std::string& text, int size,
		      const Action& action)
		: MyLabel(fh, text, size, true,
			  Color(1.0f, 1.0f, 1.0f, 1.0f),
			  Color(1.0f, 0.55f, 0.55f, 0.55f)),
		m_action(action)
	{
	}

	~MainMenuLabel() {}

	virtual void activate() {
		m_action();
	}

	virtual void cancel();

private:
	Action m_action;
};

class ToggleLabel : public MyLabel {
public:
	ToggleLabel(std::shared_ptr<Font> fh, enum bloom_setting_id id, int size);

	~ToggleLabel() {}

	virtual void activate();
	virtual void cancel();

private:
	enum bloom_setting_id m_id;
};

class MyMenu : public GenericMenu {
public:
	MyMenu(std::shared_ptr<Font> fnt, const fs::path &path);

	virtual ~MyMenu() {}

	void populate_dft();
	void populate(fs::path path, bool back, const std::string &select_name = std::string());
	void populateCredits(fs::path path);
	void populateOptions();
	void populateSettings();

	void preparePopulate(fs::path path, bool back, bool dft);
	void prepareCredits(fs::path path);
	void prepareOptions();
	void prepareSettings();
	void persistBrowsePath();
	void requestLoad(const char *path, const char *busy_msg);

	void showError(const std::string &msg);
	void showStatus(const std::string &msg);
	void clearError();
	void setChrome(const std::string &location, const std::string &hint);

	void setEntry(unsigned int entry);

	const fs::path& pwd() const { return m_path; };

	void addEntry(std::shared_ptr<MyLabel> entry);

	bool hasExited() const {
		return m_exited;
	}

	virtual void inputEvent(const Event & evt);

	virtual void startExit();

protected:
	virtual void visualPerFrame();

private:
	unsigned int pageStep() const;
	void moveSelection(int delta, bool wrap);

	bool m_input_allowed;
	Color m_color0, m_color1;
	std::vector<std::shared_ptr<MyLabel> > m_entries;
	fs::path m_path;
	unsigned int m_cursel;
	unsigned int m_font_size;
	unsigned int m_xoffset;
	unsigned int m_list_y;
	bool m_wrap;
	std::shared_ptr<Font> m_font;
	bool m_exited;
	bool m_pending_load;
	std::string m_pending_iso;
	std::shared_ptr<Background> m_bg;
	std::shared_ptr<Label> m_title;
	std::shared_ptr<Label> m_location;
	std::shared_ptr<Label> m_status;
	std::shared_ptr<Label> m_hint;

	std::shared_ptr<Scene> m_top_scene;
};


#endif /* GENMENU_H */
