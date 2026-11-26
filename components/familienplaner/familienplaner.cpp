#include "familienplaner.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "tessera/theme.h"
#include "tessera/ui.h"

namespace familienplaner {

namespace {
constexpr uint32_t kDefaultPalette[] = {
    0x3B82F6,
    0x10B981,
    0xF59E0B,
    0xEC4899,
    0x8B5CF6,
    0x06B6D4,
};
constexpr size_t kDefaultPaletteSize = sizeof(kDefaultPalette) / sizeof(kDefaultPalette[0]);
}  // namespace

void FamilienplanerPlugin::setup() {
  add_tile("agenda", [this]() { return new FamilyAgendaTile(this); });
}

FamilyAgendaTile::FamilyAgendaTile(FamilienplanerPlugin *plugin) : plugin_(plugin) {}

void FamilyAgendaTile::create(const tessera::TileContext &c) {
  ctx_ = c;

  custom_title_ = c.options["title"] | "";
  max_items_ = c.options["max_items"] | 6;
  if (max_items_ < 1) {
    max_items_ = 1;
  }
  show_calendar_ = c.options["show_calendar"] | true;

  const std::string calendars_csv = c.options["calendars"] | "";
  const std::string names_csv = c.options["calendar_names"] | "";
  const std::string colors_csv = c.options["calendar_colors"] | "";
  parse_calendar_config(calendars_csv, names_csv, colors_csv);

  root_ = lv_obj_create(c.parent);
  lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_all(root_, 8, 0);
  lv_obj_set_style_border_width(root_, 0, 0);
  lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

  header_label_ = lv_label_create(root_);
  lv_obj_align(header_label_, LV_ALIGN_TOP_LEFT, 0, 0);

  list_container_ = lv_obj_create(root_);
  lv_obj_set_pos(list_container_, 0, 24);
  lv_obj_set_size(list_container_, LV_PCT(100), LV_PCT(88));
  lv_obj_set_style_pad_all(list_container_, 0, 0);
  lv_obj_set_style_border_width(list_container_, 0, 0);
  lv_obj_set_style_bg_opa(list_container_, LV_OPA_TRANSP, 0);
  lv_obj_set_flex_flow(list_container_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list_container_, 4, 0);
  lv_obj_clear_flag(list_container_, LV_OBJ_FLAG_SCROLLABLE);

  empty_label_ = lv_label_create(root_);
  lv_obj_align(empty_label_, LV_ALIGN_TOP_LEFT, 0, 26);
  lv_obj_set_width(empty_label_, LV_PCT(100));
  lv_label_set_long_mode(empty_label_, LV_LABEL_LONG_WRAP);

  on_theme();
  on_tick(0);
}

void FamilyAgendaTile::on_state(JsonObjectConst data) {
  events_.clear();

  if (!data["title"].isNull()) {
    custom_title_ = data["title"].as<std::string>();
  }

  if (data["calendars"].is<JsonObjectConst>()) {
    JsonObjectConst cals_obj = data["calendars"].as<JsonObjectConst>();
    for (JsonPairConst kv : cals_obj) {
      const std::string cal_id = kv.key().c_str();
      if (!is_calendar_enabled(cal_id)) {
        continue;
      }
      JsonObjectConst cal_data = kv.value().as<JsonObjectConst>();
      const std::string cal_name = cal_data["name"] | "";
      JsonArrayConst items = cal_data["events"].as<JsonArrayConst>();
      if (!items.isNull()) {
        for (JsonObjectConst entry : items) {
          append_event_from_json(entry, cal_id, cal_name);
        }
      }
    }
  }

  if (data["calendars"].is<JsonArrayConst>()) {
    JsonArrayConst cals_arr = data["calendars"].as<JsonArrayConst>();
    for (JsonObjectConst cal_data : cals_arr) {
      const std::string cal_id = cal_data["entity_id"] | (cal_data["calendar"] | "");
      if (!is_calendar_enabled(cal_id)) {
        continue;
      }
      const std::string cal_name = cal_data["name"] | "";
      JsonArrayConst items = cal_data["events"].as<JsonArrayConst>();
      if (!items.isNull()) {
        for (JsonObjectConst entry : items) {
          append_event_from_json(entry, cal_id, cal_name);
        }
      }
    }
  }

  if (data["events"].is<JsonArrayConst>()) {
    JsonArrayConst items = data["events"].as<JsonArrayConst>();
    for (JsonObjectConst entry : items) {
      const std::string cal_id = entry["entity_id"] | (entry["calendar"] | "");
      if (!is_calendar_enabled(cal_id)) {
        continue;
      }
      const std::string cal_name = entry["calendar_name"] | (entry["member"] | "");
      append_event_from_json(entry, cal_id, cal_name);
    }
  }

  std::stable_sort(events_.begin(), events_.end(), [](const FamilyEventItem &a, const FamilyEventItem &b) {
    if (a.sort_key != b.sort_key && a.sort_key > 0 && b.sort_key > 0) {
      return a.sort_key < b.sort_key;
    }
    if (a.date_label != b.date_label) {
      return a.date_label < b.date_label;
    }
    return a.time < b.time;
  });

  rebuild_rows();
}

void FamilyAgendaTile::on_tick(uint32_t epoch) {
  (void) epoch;
  rebuild_rows();
}

void FamilyAgendaTile::on_theme() {
  if (root_ == nullptr) {
    return;
  }
  lv_obj_set_style_bg_color(root_, tessera::theme::card_bg(), 0);
  if (header_label_ != nullptr) {
    lv_obj_set_style_text_color(header_label_, tessera::theme::text_primary(), 0);
  }
  if (empty_label_ != nullptr) {
    lv_obj_set_style_text_color(empty_label_, tessera::theme::text_secondary(), 0);
  }
  rebuild_rows();
}

void FamilyAgendaTile::parse_calendar_config(const std::string &ids_csv,
                                             const std::string &names_csv,
                                             const std::string &colors_csv) {
  configured_calendars_.clear();
  const std::vector<std::string> ids = split_csv(ids_csv);
  const std::vector<std::string> names = split_csv(names_csv);
  const std::vector<std::string> colors = split_csv(colors_csv);

  for (size_t i = 0; i < ids.size(); ++i) {
    if (ids[i].empty()) {
      continue;
    }
    CalendarSource src;
    src.entity_id = ids[i];
    if (i < names.size() && !names[i].empty()) {
      src.display_name = names[i];
    } else {
      src.display_name = format_calendar_fallback_name(ids[i]);
    }

    uint32_t parsed_color = 0;
    if (i < colors.size() && parse_hex_color(colors[i], parsed_color)) {
      src.color_hex = parsed_color;
      src.has_custom_color = true;
    } else {
      src.color_hex = kDefaultPalette[i % kDefaultPaletteSize];
      src.has_custom_color = true;
    }
    configured_calendars_.push_back(src);
  }
}

bool FamilyAgendaTile::is_calendar_enabled(const std::string &entity_id) const {
  if (configured_calendars_.empty()) {
    return true;
  }
  if (entity_id.empty()) {
    return false;
  }
  for (const auto &src : configured_calendars_) {
    if (src.entity_id == entity_id) {
      return true;
    }
  }
  return false;
}

const CalendarSource *FamilyAgendaTile::find_calendar_source(const std::string &entity_id) const {
  for (const auto &src : configured_calendars_) {
    if (src.entity_id == entity_id) {
      return &src;
    }
  }
  return nullptr;
}

void FamilyAgendaTile::append_event_from_json(JsonObjectConst entry,
                                              const std::string &fallback_calendar_id,
                                              const std::string &fallback_calendar_name) {
  const std::string title = entry["title"] | (entry["summary"] | "");
  if (title.empty()) {
    return;
  }

  FamilyEventItem item;
  item.title = title;
  item.time = entry["time"] | "";
  item.date_label = entry["date"] | "";
  item.sort_key = entry["start_epoch"] | 0;
  item.calendar_id = entry["entity_id"] | (entry["calendar"] | fallback_calendar_id);

  const CalendarSource *src = find_calendar_source(item.calendar_id);
  if (src != nullptr) {
    item.calendar_name = src->display_name;
    item.accent_color = src->color_hex;
    item.has_accent_color = src->has_custom_color;
  } else {
    if (!fallback_calendar_name.empty()) {
      item.calendar_name = fallback_calendar_name;
    } else if (!item.calendar_id.empty()) {
      item.calendar_name = format_calendar_fallback_name(item.calendar_id);
    }
    uint32_t json_color = 0;
    const std::string color_str = entry["color"] | "";
    if (!color_str.empty() && parse_hex_color(color_str, json_color)) {
      item.accent_color = json_color;
      item.has_accent_color = true;
    } else {
      item.accent_color = kDefaultPalette[events_.size() % kDefaultPaletteSize];
      item.has_accent_color = true;
    }
  }

  events_.push_back(item);
}

void FamilyAgendaTile::clear_row_widgets() {
  if (list_container_ == nullptr) {
    return;
  }
  lv_obj_clean(list_container_);
}

void FamilyAgendaTile::rebuild_rows() {
  if (header_label_ == nullptr || list_container_ == nullptr || empty_label_ == nullptr) {
    return;
  }

  std::string heading = custom_title_.empty() ? ctx_.tr("screen.header_fallback") : custom_title_;
  lv_label_set_text(header_label_, heading.c_str());

  clear_row_widgets();

  if (events_.empty()) {
    lv_obj_clear_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(empty_label_, ctx_.tr("screen.empty_day"));
    return;
  }

  lv_obj_add_flag(empty_label_, LV_OBJ_FLAG_HIDDEN);

  const size_t limit = std::min<size_t>(events_.size(), static_cast<size_t>(max_items_));
  for (size_t i = 0; i < limit; ++i) {
    const FamilyEventItem &ev = events_[i];

    lv_obj_t *row = lv_obj_create(list_container_);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *pill = lv_obj_create(row);
    lv_obj_set_size(pill, 4, 16);
    lv_obj_set_style_radius(pill, 2, 0);
    lv_obj_set_style_border_width(pill, 0, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(ev.accent_color), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);

    std::string text;
    if (!ev.date_label.empty()) {
      text += ev.date_label + " ";
    }
    if (!ev.time.empty()) {
      text += ev.time + " ";
    }
    text += ev.title;
    if (show_calendar_ && !ev.calendar_name.empty()) {
      text += " · " + ev.calendar_name;
    }

    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_set_flex_grow(lbl, 1);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(lbl, tessera::theme::text_primary(), 0);
    lv_label_set_text(lbl, text.c_str());
  }
}

std::vector<std::string> FamilyAgendaTile::split_csv(const std::string &input) {
  std::vector<std::string> result;
  size_t start = 0;
  while (start <= input.size()) {
    size_t comma = input.find(',', start);
    if (comma == std::string::npos) {
      result.push_back(trim(input.substr(start)));
      break;
    }
    result.push_back(trim(input.substr(start, comma - start)));
    start = comma + 1;
  }
  return result;
}

std::string FamilyAgendaTile::trim(const std::string &s) {
  size_t first = 0;
  while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) {
    ++first;
  }
  size_t last = s.size();
  while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) {
    --last;
  }
  return s.substr(first, last - first);
}

bool FamilyAgendaTile::parse_hex_color(const std::string &hex, uint32_t &out_color) {
  std::string clean = trim(hex);
  if (!clean.empty() && clean[0] == '#') {
    clean.erase(0, 1);
  }
  if (clean.size() != 6) {
    return false;
  }
  char *endptr = nullptr;
  unsigned long val = std::strtoul(clean.c_str(), &endptr, 16);
  if (endptr == nullptr || *endptr != ' ') {
    return false;
  }
  out_color = static_cast<uint32_t>(val & 0xFFFFFF);
  return true;
}

std::string FamilyAgendaTile::format_calendar_fallback_name(const std::string &entity_id) {
  std::string name = entity_id;
  const size_t dot = name.find('.');
  if (dot != std::string::npos) {
    name = name.substr(dot + 1);
  }
  for (char &ch : name) {
    if (ch == '_') {
      ch = ' ';
    }
  }
  if (!name.empty()) {
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
  }
  return name;
}

}  // namespace familienplaner
