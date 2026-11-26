#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "esphome/core/component.h"
#include "tessera/plugin.h"
#include "tessera/tile.h"

namespace familienplaner {

struct CalendarSource {
  std::string entity_id;
  std::string display_name;
  uint32_t color_hex{0x3B82F6};
  bool has_custom_color{false};
};

struct FamilyEventItem {
  int64_t sort_key{0};
  std::string date_label;
  std::string time;
  std::string title;
  std::string calendar_id;
  std::string calendar_name;
  uint32_t accent_color{0x3B82F6};
  bool has_accent_color{false};
};

class FamilienplanerPlugin : public esphome::Component, public tessera::Plugin {
 public:
  void setup() override;
};

class FamilyAgendaTile : public tessera::Tile {
 public:
  explicit FamilyAgendaTile(FamilienplanerPlugin *plugin);

  void create(const tessera::TileContext &c) override;
  void on_state(JsonObjectConst data) override;
  void on_tick(uint32_t epoch) override;
  void on_theme() override;

 private:
  void parse_calendar_config(const std::string &ids_csv,
                             const std::string &names_csv,
                             const std::string &colors_csv);
  bool is_calendar_enabled(const std::string &entity_id) const;
  const CalendarSource *find_calendar_source(const std::string &entity_id) const;
  void append_event_from_json(JsonObjectConst entry,
                              const std::string &fallback_calendar_id,
                              const std::string &fallback_calendar_name);
  void rebuild_rows();
  void clear_row_widgets();

  static std::vector<std::string> split_csv(const std::string &input);
  static std::string trim(const std::string &s);
  static bool parse_hex_color(const std::string &hex, uint32_t &out_color);
  static std::string format_calendar_fallback_name(const std::string &entity_id);

  FamilienplanerPlugin *plugin_{nullptr};
  tessera::TileContext ctx_{};
  std::string custom_title_{};
  int max_items_{6};
  bool show_calendar_{true};

  std::vector<CalendarSource> configured_calendars_{};
  std::vector<FamilyEventItem> events_{};

  lv_obj_t *root_{nullptr};
  lv_obj_t *header_label_{nullptr};
  lv_obj_t *list_container_{nullptr};
  lv_obj_t *empty_label_{nullptr};
};

}  // namespace familienplaner
