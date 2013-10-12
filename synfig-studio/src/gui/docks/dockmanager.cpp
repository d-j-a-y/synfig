/* === S Y N F I G ========================================================= */
/*!	\file dockmanager.cpp
**	\brief Template File
**
**	$Id$
**
**	\legal
**	Copyright (c) 2002-2005 Robert B. Quattlebaum Jr., Adrian Bentley
**	Copyright (c) 2007, 2008 Chris Moore
**
**	This package is free software; you can redistribute it and/or
**	modify it under the terms of the GNU General Public License as
**	published by the Free Software Foundation; either version 2 of
**	the License, or (at your option) any later version.
**
**	This package is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**	General Public License for more details.
**	\endlegal
*/
/* ========================================================================= */

/* === H E A D E R S ======================================================= */

#ifdef USING_PCH
#	include "pch.h"
#else
#ifdef HAVE_CONFIG_H
#	include <config.h>
#endif

#include "docks/dockmanager.h"
#include <stdexcept>
#include "docks/dockable.h"
#include "docks/dockbook.h"
#include "docks/dockdialog.h"
#include <synfigapp/settings.h>
#include <synfigapp/main.h>
#include <gdkmm/general.h>

#include "general.h"

#include <gtkmm/paned.h>
#include <gtkmm/box.h>
#include <gtkmm/window.h>

#include "app.h"
#include "mainwindow.h"

#endif

/* === U S I N G =========================================================== */

using namespace std;
using namespace etl;
using namespace synfig;
using namespace studio;

/* === M A C R O S ========================================================= */

/* === P R O C E D U R E S ================================================= */

namespace studio {
	class DockLinkPoint {
	public:
		Gtk::Bin *bin;
		Gtk::Paned *paned;
		Gtk::Window *window;
		bool is_first;

		DockLinkPoint(): bin(NULL), paned(NULL), window(NULL), is_first(false) { }
		explicit DockLinkPoint(Gtk::Bin *bin): bin(bin), paned(NULL), window(NULL), is_first(false) { }
		explicit DockLinkPoint(Gtk::Paned *paned, bool is_first): bin(NULL), paned(paned), window(NULL), is_first(is_first) { }
		explicit DockLinkPoint(Gtk::Window *window): bin(NULL), paned(NULL), window(window), is_first(false) { }
		explicit DockLinkPoint(Gtk::Widget &widget) {
			Gtk::Container *container = widget.get_parent();
			bin = dynamic_cast<Gtk::Bin*>(container);
			paned = dynamic_cast<Gtk::Paned*>(container);
			window = dynamic_cast<Gtk::Window*>(container);
			is_first = paned != NULL && paned->get_child1() == &widget;
		}

		bool is_valid() { return bin || paned || window; }

		void unlink() {
			if (paned && is_first && paned->get_child1())
				paned->remove(*paned->get_child1());
			else
			if (paned && !is_first && paned->get_child2())
				paned->remove(*paned->get_child2());
			else
			if (window)
				window->remove();
			if (bin)
				bin->remove();
		}

		void link(Gtk::Widget &widget)
		{
			if (paned && is_first)
				paned->add1(widget);
			else
			if (paned && !is_first)
				paned->add2(widget);
			else
			if (window)
				window->add(widget);
			else
			if (bin)
				bin->add(widget);
		}
	};
}

class studio::DockSettings : public synfigapp::Settings
{
	DockManager* dock_manager;

public:
	DockSettings(DockManager* dock_manager):dock_manager(dock_manager)
	{
		synfigapp::Main::settings().add_domain(this,"dock");
	}

	virtual ~DockSettings()
	{
		synfigapp::Main::settings().remove_domain("dock");
	}

	virtual bool get_value(const synfig::String& key_, synfig::String& value)const
	{
		try
		{
			if (key_ == "layout")
			{
				value = dock_manager->save_layout_to_string();
				return true;
			}
		}catch (...) { return false; }
		return synfigapp::Settings::get_value(key_,value);
	}

	virtual bool set_value(const synfig::String& key_,const synfig::String& value)
	{
		try
		{
			if (key_ == "layout")
			{
				dock_manager->load_layout_from_string(value);
				return true;
			}
		}catch (...) { return false; }
		return synfigapp::Settings::set_value(key_,value);
	}

	virtual KeyList get_key_list()const
	{
		synfigapp::Settings::KeyList ret(synfigapp::Settings::get_key_list());
		ret.push_back("layout");
		return ret;
	}
};

/* === M E T H O D S ======================================================= */

DockManager::DockManager():
	dock_settings(new DockSettings(this))
{
}

DockManager::~DockManager()
{
	while(!dock_dialog_list_.empty())
	{
		dock_dialog_list_.back()->close();
	}
	while(!dockable_list_.empty())
	{
		Dockable* dockable(dockable_list_.back());
		// synfig::info("DockManager::~DockManager(): Deleting dockable \"%s\"",dockable->get_name().c_str());
		dockable_list_.pop_back();
		delete dockable;
	}
}

void
DockManager::register_dockable(Dockable& x)
{
	dockable_list_.push_back(&x);
	// synfig::info("DockManager::register_dockable(): Registered dockable \"%s\"",dockable_list_.back()->get_name().c_str());
	signal_dockable_registered()(&x);
}

bool
DockManager::unregister_dockable(Dockable& x)
{
	std::list<Dockable*>::iterator iter;
	for(iter=dockable_list_.begin();iter!=dockable_list_.end();++iter)
	{
		if(&x==*iter)
		{
			remove_widget_recursive(x);
			dockable_list_.erase(iter);
			synfig::info("DockManager::unregister_dockable(): \"%s\" has been Unregistered",x.get_name().c_str());
			return true;
		}
	}
	return false;
}

Dockable&
DockManager::find_dockable(const synfig::String& x)
{
	std::list<Dockable*>::iterator iter;
	for(iter=dockable_list_.begin();iter!=dockable_list_.end();++iter)
		if((*iter)->get_name()==x)
			return **iter;

	throw std::runtime_error("DockManager::find_dockable(): not found");
}

void
DockManager::present(synfig::String x)
{
	try
	{
		find_dockable(x).present();
	}
	catch(...)
	{
	}
}

DockDialog&
DockManager::find_dock_dialog(int id)
{
	std::list<DockDialog*>::iterator iter;
	for(iter=dock_dialog_list_.begin();iter!=dock_dialog_list_.end();++iter)
		if((*iter)->get_id()==id)
			return **iter;

	DockDialog* dock_dialog(new DockDialog());
	dock_dialog->set_id(id);
	return *dock_dialog;
}

const DockDialog&
DockManager::find_dock_dialog(int id)const
{
	std::list<DockDialog*>::const_iterator iter;
	for(iter=dock_dialog_list_.begin();iter!=dock_dialog_list_.end();++iter)
		if((*iter)->get_id()==id)
			return **iter;

	throw std::runtime_error("DockManager::find_dock_dialog(int id)const: not found");
}

void
DockManager::show_all_dock_dialogs()
{
	std::list<DockDialog*>::iterator iter;
	for(iter=dock_dialog_list_.begin();iter!=dock_dialog_list_.end();++iter)
		(*iter)->present();
}

bool
DockManager::swap_widgets(Gtk::Widget &widget1, Gtk::Widget &widget2)
{
	DockLinkPoint point1(widget1);
	DockLinkPoint point2(widget2);
	if (point1.is_valid() && point2.is_valid())
	{
		point1.unlink();
		point2.unlink();
		point1.link(widget2);
		point2.link(widget1);
		return true;
	}
	return false;
}

void
DockManager::remove_widget_recursive(Gtk::Widget &widget)
{
	DockLinkPoint link(widget);
	if (link.is_valid())
	{
		link.unlink();
		if (link.paned)
		{
			Gtk::Widget &widget = link.is_first
								? *link.paned->get_child2()
								: *link.paned->get_child1();
			DockLinkPoint paned_link(*link.paned);
			if (paned_link.is_valid())
			{
				link.paned->remove(widget);
				paned_link.unlink();
				paned_link.link(widget);
				delete link.paned;
			}
		}
		else
		if (link.window) link.window->hide();
	}
	else
	if (widget.get_parent())
	{
		DockBook *book = dynamic_cast<DockBook*>(widget.get_parent());
		widget.get_parent()->remove(widget);
		if (book && book->pages().empty())
		{
			remove_widget_recursive(*book);
			delete book;
		}
	}
}


bool
DockManager::add_widget(Gtk::Widget &dest_widget, Gtk::Widget &src_widget, bool vertical, bool first)
{
	if (&src_widget == &dest_widget) return false;

	// check for src widget is parent for dest_widget
	for(Gtk::Widget *parent = src_widget.get_parent(); parent != NULL; parent = parent->get_parent())
		if (parent == &dest_widget)
			return swap_widgets(src_widget, dest_widget);

	// unlink dest_widget
	DockLinkPoint dest_link(dest_widget);
	if (!dest_link.is_valid()) return false;
	dest_link.unlink();

	// unlink src_widget
	remove_widget_recursive(src_widget);

	// create new paned and link all
	Gtk::Paned *paned = manage(vertical ? (Gtk::Paned*)new Gtk::VPaned() : (Gtk::Paned*)new Gtk::HPaned());
	paned->show();
	DockLinkPoint(paned, first).link(src_widget);
	DockLinkPoint(paned, !first).link(dest_widget);
	dest_link.link(*paned);
	return true;
}

bool
DockManager::add_dockable(Gtk::Widget &dest_widget, Dockable &dockable, bool vertical, bool first)
{
	DockBook *book = manage(new DockBook());
	book->show();
	if (add_widget(dest_widget, *book, vertical, first))
	{
		book->add(dockable);
		return true;
	}
	delete book;
	return false;
}

bool DockManager::read_separator(std::string &x)
{
	size_t pos = x.find_first_of("|]");
	if (pos == std::string::npos) { x.clear(); return false; }
	if (x[pos] == '|') { x = x.substr(pos+1); return true; }
	if (x[pos] == ']') x = x.substr(pos+1);
	return false;
}

std::string DockManager::read_string(std::string &x)
{
	size_t pos = x.find_first_of("|]");
	std::string res = x.substr(0, pos);
	if (pos == std::string::npos) x.clear(); else x = x.substr(pos);
	return res;
}

int DockManager::read_int(std::string &x)
{
	return strtol(read_string(x).c_str(), NULL, 10);
}

bool DockManager::read_bool(std::string &x)
{
	return read_string(x) == "true";
}

Gtk::Widget* DockManager::read_widget(std::string &x)
{
	bool hor = x.substr(0, 5) == "[hor|";
	bool vert = x.substr(0, 6) == "[vert|";

	// paned
	if (hor || vert)
	{
		// skip "[hor|" or "[vert|"
		x = x.substr(1);
		if (!read_separator(x)) return NULL;

		int size = read_int(x);
		if (!read_separator(x)) return NULL;

		Gtk::Widget *first = NULL;
		Gtk::Widget *second = NULL;

		first = read_widget(x);
		if (!read_separator(x)) return first;
		second = read_widget(x);
		read_separator(x);

		if (!first && !second) return NULL;
		if (first && !second) return first;
		if (!first && second) return second;

		// create paned
		Gtk::Paned *paned = manage(hor ? (Gtk::Paned*)new Gtk::HPaned() : (Gtk::Paned*)new Gtk::VPaned());
		paned->add1(*first);
		paned->add2(*second);
		paned->set_position(size);
		paned->show();
		return paned;
	}
	else
	if (x.substr(0, 6) == "[book|")
	{
		// skip "[book|"
		x = x.substr(1);
		if (!read_separator(x)) return NULL;

		DockBook *book = NULL;
		do
		{
			std::string name = read_string(x);
			if (!name.empty())
			{
				Dockable *dockable = &find_dockable(name);
				if (dockable != NULL)
				{
					if (book == NULL) book = manage(new DockBook());
					book->add(*dockable);
				}
			}
		} while (read_separator(x));

		return book;
	}
	else
	if (x.substr(0, 8) == "[dialog|")
	{
		// skip "[dialog|"
		x = x.substr(1);
		if (!read_separator(x)) return NULL;

		int left = read_int(x);
		if (!read_separator(x)) return NULL;
		int top = read_int(x);
		if (!read_separator(x)) return NULL;
		int width = read_int(x);
		if (!read_separator(x)) return NULL;
		int height = read_int(x);
		if (!read_separator(x)) return NULL;

		Gtk::Widget *widget = read_widget(x);
		read_separator(x);

		if (!widget) return NULL;

		DockDialog *dialog = new DockDialog();
		dialog->add(*widget);
		dialog->move(left, top);
		dialog->set_default_size(width, height);
		dialog->resize(width, height);

		return NULL;
	}
	else
	if (x.substr(0, 12) == "[mainwindow|")
	{
		// skip "[dialog|"
		x = x.substr(1);
		if (!read_separator(x)) return NULL;

		int left = read_int(x);
		if (!read_separator(x)) return NULL;
		int top = read_int(x);
		if (!read_separator(x)) return NULL;
		int width = read_int(x);
		if (!read_separator(x)) return NULL;
		int height = read_int(x);
		if (!read_separator(x)) return NULL;

		Gtk::Widget *widget = read_widget(x);
		read_separator(x);

		if (!widget) return NULL;

		Gtk::Widget *child = App::main_window->root().get_child();
		App::main_window->root().remove();
		if (child && child != &App::main_window->notebook())
			delete child;
		App::main_window->root().add(*widget);

		App::main_window->move(left, top);
		App::main_window->set_default_size(width, height);
		App::main_window->resize(width, height);

		return NULL;
	}
	else
	if (x.substr(0, 14) == "[mainnotebook]")
	{
		x = x.substr(14);
		if (App::main_window->notebook().get_parent())
			App::main_window->notebook().get_parent()->remove(App::main_window->notebook());
		return &App::main_window->notebook();
	}

	return NULL;
}

void DockManager::write_string(std::string &x, const std::string &str)
	{ x += str; }
void DockManager::write_separator(std::string &x, bool continue_)
	{ write_string(x, continue_ ? "|" : "]"); }
void DockManager::write_int(std::string &x, int i)
	{ write_string(x, strprintf("%d", i)); }
void DockManager::write_bool(std::string &x, bool b)
	{ write_string(x, b ? "true" : "false"); }

void DockManager::write_widget(std::string &x, Gtk::Widget* widget)
{
	Gtk::Paned *paned = dynamic_cast<Gtk::Paned*>(widget);
	Gtk::HPaned *hpaned = dynamic_cast<Gtk::HPaned*>(widget);
	DockBook *book = dynamic_cast<DockBook*>(widget);
	DockDialog *dialog = dynamic_cast<DockDialog*>(widget);

	if (widget == NULL)
	{
		return;
	}
	else
	if (widget == App::main_window)
	{
		write_string(x, "[mainwindow|");
		int left = 0, top = 0, width = 0, height = 0;
		App::main_window->get_position(left, top);
		App::main_window->get_size(width, height);
		write_int(x, left);
		write_separator(x);
		write_int(x, top);
		write_separator(x);
		write_int(x, width);
		write_separator(x);
		write_int(x, height);
		write_separator(x);

		write_widget(x, App::main_window->root().get_child());
		write_separator(x, false);
	}
	else
	if (widget == &App::main_window->notebook())
	{
		write_string(x, "[mainnotebook]");
	}
	else
	if (dialog)
	{
		write_string(x, "[dialog|");
		int left = 0, top = 0, width = 0, height = 0;
		dialog->get_position(left, top);
		dialog->get_size(width, height);
		write_int(x, left);
		write_separator(x);
		write_int(x, top);
		write_separator(x);
		write_int(x, width);
		write_separator(x);
		write_int(x, height);
		write_separator(x);

		write_widget(x, dialog->get_child());
		write_separator(x, false);
	}
	else
	if (paned)
	{
		write_string(x, hpaned ? "[hor|" : "[vert|");
		write_int(x, paned->get_position());
		write_separator(x);
		write_widget(x, paned->get_child1());
		write_separator(x);
		write_widget(x, paned->get_child2());
		write_separator(x, false);
	}
	else
	if (book)
	{
		write_string(x, "[book");
		Gtk::Notebook::PageList &pages = book->pages();
		for(Gtk::Notebook::PageList::iterator i = pages.begin(); i != pages.end(); i++)
		{
			Dockable *dockable = dynamic_cast<Dockable*>(i->get_child());
			if (dockable)
			{
				write_separator(x);
				write_string(x, dockable->get_name());
			}
		}
		write_separator(x, false);
	}
}

std::string DockManager::save_widget_to_string(Gtk::Widget *widget)
{
	std::string res;
	write_widget(res, widget);
	return res;
}

Gtk::Widget* DockManager::load_widget_from_string(const std::string &x)
{
	std::string copy(x);
	return read_widget(copy);
}

std::string DockManager::save_layout_to_string()
{
	std::string res;
	write_widget(res, App::main_window);
	for(std::list<DockDialog*>::iterator i = dock_dialog_list_.begin(); i != dock_dialog_list_.end(); i++)
	{
		write_separator(res);
		write_widget(res, *i);
	}
	return res;
}

void DockManager::load_layout_from_string(const std::string &x)
{
	std::string copy(x);
	do
	{
		read_widget(copy);
	} while (read_separator(copy));
}



