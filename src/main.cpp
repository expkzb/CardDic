#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <M5Cardputer.h>
#include <ctype.h>

#define SD_SPI_SCK_PIN 40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN 12

static const char *kIndexPath = "/carddic/index.bin";
static const char *kEntriesPath = "/carddic/entries.dat";
static const char *kPrefixPath = "/carddic/prefix.bin";
static const char *kIpaFontPath = "/carddic/ipa.bff";
static const uint32_t kIndexMagic = 0x43494443;  // CDIC, little endian.
static const uint32_t kPrefixMagic = 0x58465043;  // CPFX, little endian.
static const uint16_t kIndexVersion = 1;
static const uint8_t kMaxResults = 3;
static const uint8_t kTopH = 24;
static const uint8_t kBottomH = 22;
static const uint8_t kLineH = 19;
static const uint8_t kRowH = 21;
static const uint16_t kSearchDelayMs = 450;

#pragma pack(push, 1)
struct IndexHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t record_size;
  uint32_t count;
};

struct IndexRecord {
  char word[32];
  uint32_t offset;
  uint32_t length;
  char preview[64];
};

struct PrefixHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t record_size;
  uint32_t count;
};

struct PrefixRecord {
  uint32_t start;
  uint32_t count;
};
#pragma pack(pop)

struct Entry {
  String word;
  String phonetic;
  String translation;
  String example_en;
  String example_zh;
};

enum class ViewMode {
  Search,
  Detail,
  Error,
};

static IndexHeader g_header = {};
static String g_query;
static String g_status;
static IndexRecord g_results[kMaxResults];
static uint8_t g_result_count = 0;
static uint8_t g_selected = 0;
static ViewMode g_view = ViewMode::Search;
static Entry g_entry;
static String g_detail_lines[48];
static uint8_t g_detail_line_count = 0;
static uint8_t g_detail_top = 0;
static bool g_needs_redraw = true;
static bool g_input_needs_redraw = false;
static bool g_input_append_redraw = false;
static bool g_input_delete_redraw = false;
static bool g_detail_body_needs_redraw = false;
static char g_input_append_char = '\0';
static uint8_t g_input_delete_pos = 0;
static bool g_results_needs_redraw = false;
static bool g_search_pending = false;
static bool g_search_active = false;
static String g_search_prefix;
static uint32_t g_search_pos = 0;
static uint32_t g_search_end = 0;
static uint32_t g_last_input_ms = 0;
static uint32_t g_last_key_ms = 0;
static File g_index_file;
static File g_prefix_file;
static File g_ipa_font_file;
static lgfx::DataWrapperT<fs::File> g_ipa_font_data(&g_ipa_font_file);
static lgfx::BFFfont g_ipa_font;
static bool g_ipa_font_loaded = false;

static int16_t screenW() {
  return M5Cardputer.Display.width();
}

static int16_t screenH() {
  return M5Cardputer.Display.height();
}

static String lowerAscii(String value) {
  value.toLowerCase();
  return value;
}

static int prefixCharValue(char c) {
  if (c >= 'a' && c <= 'z') {
    return c - 'a' + 1;
  }
  if (c >= '0' && c <= '9') {
    return c - '0' + 27;
  }
  if (c == '-') {
    return 37;
  }
  if (c == '\'') {
    return 38;
  }
  return -1;
}

static uint32_t prefixCode(const String &prefix) {
  uint32_t code = 0;
  for (uint8_t i = 0; i < 3; ++i) {
    int value = 0;
    if (i < prefix.length()) {
      value = prefixCharValue(prefix[i]);
      if (value < 0) {
        return UINT32_MAX;
      }
    }
    code = code * 39 + value;
  }
  return code;
}

static uint8_t visibleDetailLines() {
  int16_t area = screenH() - kTopH - kBottomH - 4;
  if (area <= 0) {
    return 0;
  }
  return area / kLineH;
}

static String normalizePhonetic(const String &text) {
  return text;
}

static bool fileExists(const char *path) {
  File f = SD.open(path, FILE_READ);
  if (!f) {
    return false;
  }
  f.close();
  return true;
}

static void showFatal(const String &message) {
  g_status = message;
  g_view = ViewMode::Error;
  g_needs_redraw = true;
}

static bool readRecord(uint32_t pos, IndexRecord &record) {
  const uint32_t offset = sizeof(IndexHeader) + pos * sizeof(IndexRecord);
  if (!g_index_file.seek(offset)) {
    return false;
  }
  return g_index_file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) == sizeof(record);
}

static bool startsWithPrefix(const char *word, const String &prefix) {
  return strncmp(word, prefix.c_str(), prefix.length()) == 0;
}

static bool readPrefixRange(const String &query, PrefixRecord &range) {
  String prefix = query.substring(0, min(static_cast<uint8_t>(3), static_cast<uint8_t>(query.length())));
  uint32_t code = prefixCode(prefix);
  if (code == UINT32_MAX) {
    return false;
  }
  uint32_t offset = sizeof(PrefixHeader) + code * sizeof(PrefixRecord);
  if (!g_prefix_file.seek(offset)) {
    return false;
  }
  return g_prefix_file.read(reinterpret_cast<uint8_t *>(&range), sizeof(range)) == sizeof(range);
}

static bool hasHidKey(const Keyboard_Class::KeysState &keys, uint8_t hid) {
  for (auto key : keys.hid_keys) {
    if (key == hid) {
      return true;
    }
  }
  return false;
}

static bool hasWordKey(const Keyboard_Class::KeysState &keys, char first, char second) {
  for (auto c : keys.word) {
    if (c == first || c == second) {
      return true;
    }
  }
  return false;
}

static bool navUp(const Keyboard_Class::KeysState &keys) {
  return hasHidKey(keys, 0x52) || hasWordKey(keys, ';', ':');
}

static bool navDown(const Keyboard_Class::KeysState &keys) {
  return hasHidKey(keys, 0x51) || hasWordKey(keys, '.', '>');
}

static void beginSearch() {
  g_result_count = 0;
  g_selected = 0;
  g_status = "";
  g_search_active = false;

  g_search_prefix = lowerAscii(g_query);
  if (g_search_prefix.length() == 0) {
    return;
  }

  if (!g_index_file) {
    showFatal("Cannot open index.bin");
    return;
  }

  PrefixRecord range = {};
  if (!readPrefixRange(g_search_prefix, range) || range.count == 0) {
    g_status = "No match";
    g_results_needs_redraw = true;
    return;
  }

  g_search_pos = range.start;
  g_search_end = min(range.start + range.count, g_header.count);
  g_search_active = true;
}

static void searchStep() {
  if (!g_search_active) {
    return;
  }

  const uint8_t max_steps = 4;
  for (uint8_t step = 0; step < max_steps && g_search_pos < g_search_end &&
                         g_result_count < kMaxResults;
       ++step, ++g_search_pos) {
    IndexRecord record;
    if (!readRecord(g_search_pos, record)) {
      break;
    }
    if (!startsWithPrefix(record.word, g_search_prefix)) {
      continue;
    }
    g_results[g_result_count++] = record;
  }

  if (g_search_pos >= g_search_end || g_result_count >= kMaxResults) {
    g_search_active = false;
    g_results_needs_redraw = true;
  }

  if (!g_search_active && g_result_count == 0) {
    g_status = "No match";
    g_results_needs_redraw = true;
  }
}

static String fieldAt(const String &body, int field_index) {
  int start = 0;
  for (int i = 0; i < field_index; ++i) {
    int sep = body.indexOf('\x1e', start);
    if (sep < 0) {
      return "";
    }
    start = sep + 1;
  }
  int end = body.indexOf('\x1e', start);
  if (end < 0) {
    end = body.length();
  }
  return body.substring(start, end);
}

static bool loadEntry(const IndexRecord &record) {
  File entries = SD.open(kEntriesPath, FILE_READ);
  if (!entries) {
    showFatal("Cannot open entries.dat");
    return false;
  }
  if (!entries.seek(record.offset)) {
    entries.close();
    showFatal("Bad entry offset");
    return false;
  }
  String body;
  body.reserve(record.length + 1);
  for (uint32_t i = 0; i < record.length && entries.available(); ++i) {
    body += static_cast<char>(entries.read());
  }
  entries.close();

  g_entry.word = fieldAt(body, 0);
  g_entry.phonetic = normalizePhonetic(fieldAt(body, 1));
  g_entry.translation = fieldAt(body, 2);
  g_entry.example_en = fieldAt(body, 3);
  g_entry.example_zh = fieldAt(body, 4);
  return true;
}

static void pushLine(const String &line) {
  if (g_detail_line_count < 48) {
    g_detail_lines[g_detail_line_count++] = line;
  }
}

static void wrapText(const String &label, const String &text, uint8_t max_units) {
  if (text.length() == 0) {
    return;
  }

  pushLine(label);
  String line;
  uint8_t units = 0;
  for (uint16_t i = 0; i < text.length();) {
    unsigned char c = text[i];
    uint8_t bytes = 1;
    uint8_t width = 1;
    if ((c & 0xE0) == 0xC0) {
      bytes = 2;
      width = 2;
    } else if ((c & 0xF0) == 0xE0) {
      bytes = 3;
      width = 2;
    } else if ((c & 0xF8) == 0xF0) {
      bytes = 4;
      width = 2;
    }

    if (units + width > max_units && line.length() > 0) {
      pushLine(line);
      line = "";
      units = 0;
    }

    line += text.substring(i, i + bytes);
    units += width;
    i += bytes;
  }
  if (line.length() > 0) {
    pushLine(line);
  }
}

static void buildDetailLines() {
  g_detail_line_count = 0;
  g_detail_top = 0;
  String title = g_entry.word;
  if (g_entry.phonetic.length() > 0) {
    title += "  ";
    title += g_entry.phonetic;
  }
  pushLine(title);
  wrapText("释义", g_entry.translation, 24);
  wrapText("例句", g_entry.example_en, 30);
  wrapText("译文", g_entry.example_zh, 24);
}

static void drawTopBar(const String &title) {
  auto &d = M5Cardputer.Display;
  d.fillRect(0, 0, screenW(), kTopH, TFT_NAVY);
  d.setTextColor(TFT_WHITE, TFT_NAVY);
  d.setCursor(4, 4);
  d.print(title);
}

static void drawBottomBar(const String &hint) {
  auto &d = M5Cardputer.Display;
  int16_t y = screenH() - kBottomH;
  d.fillRect(0, y, screenW(), kBottomH, TFT_DARKGREY);
  d.setTextColor(TFT_WHITE, TFT_DARKGREY);
  d.setCursor(4, y + 3);
  d.print(hint);
}

static void drawSearchInput() {
  auto &d = M5Cardputer.Display;
  int y = kTopH + 2;
  d.fillRect(0, y, screenW(), 22, TFT_BLACK);
  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.setCursor(4, kTopH + 4);
  d.print("> ");
  d.print(g_query);
}

static int16_t inputCharX(uint8_t index) {
  String prefix = "> ";
  for (uint8_t i = 0; i < index && i < g_query.length(); ++i) {
    prefix += g_query[i];
  }
  return 4 + M5Cardputer.Display.textWidth(prefix);
}

static void drawSearchInputAppend(char c) {
  auto &d = M5Cardputer.Display;
  d.setTextColor(TFT_CYAN, TFT_BLACK);
  d.setCursor(inputCharX(g_query.length() - 1), kTopH + 4);
  d.print(c);
}

static void drawSearchInputDelete(uint8_t pos) {
  auto &d = M5Cardputer.Display;
  int16_t x = inputCharX(pos);
  d.fillRect(x, kTopH + 2, screenW() - x, 22, TFT_BLACK);
}

static void drawSearchResults() {
  auto &d = M5Cardputer.Display;
  int16_t top = kTopH + 24;
  int16_t bottom = screenH() - kBottomH;
  d.fillRect(0, top, screenW(), bottom - top, TFT_BLACK);

  int y = kTopH + 26;
  for (uint8_t i = 0; i < g_result_count; ++i) {
    uint16_t bg = (i == g_selected) ? TFT_DARKCYAN : TFT_BLACK;
    uint16_t fg = (i == g_selected) ? TFT_WHITE : TFT_LIGHTGREY;
    d.fillRect(2, y - 2, screenW() - 4, kRowH, bg);
    d.setTextColor(fg, bg);
    d.setCursor(5, y);
    d.print(g_results[i].word);
    y += kRowH;
  }

  if (g_status.length() > 0) {
    d.setTextColor(TFT_ORANGE, TFT_BLACK);
    d.setCursor(5, kTopH + 46);
    d.print(g_status);
  } else if (g_query.length() == 0) {
    d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    d.setCursor(5, kTopH + 46);
    d.print("Type English to search");
  }
}

static void drawSearch() {
  auto &d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  drawTopBar("CardDic");
  drawSearchInput();
  drawSearchResults();
  drawBottomBar("Ent Detail  ;/. Move  Esc Clear");
}

static void drawDetail() {
  auto &d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  drawTopBar("Detail");

  d.setTextColor(TFT_WHITE, TFT_BLACK);
  int y = kTopH + 4;
  const uint8_t visible_lines = visibleDetailLines();
  for (uint8_t i = 0; i < visible_lines; ++i) {
    uint8_t line_index = g_detail_top + i;
    if (line_index >= g_detail_line_count) {
      break;
    }
    if (g_detail_lines[line_index] == "释义" || g_detail_lines[line_index] == "例句" ||
        g_detail_lines[line_index] == "译文") {
      d.setTextColor(TFT_YELLOW, TFT_BLACK);
    } else {
      d.setTextColor(TFT_WHITE, TFT_BLACK);
    }
    d.setCursor(4, y);
    d.print(g_detail_lines[line_index]);
    y += kLineH;
  }

  drawBottomBar(",/. Scroll  Esc Back");
}

static bool isDetailLabel(const String &line) {
  return line == "释义" || line == "例句" || line == "译文";
}

static void drawDetailBody() {
  auto &d = M5Cardputer.Display;
  int16_t top = kTopH;
  int16_t bottom = screenH() - kBottomH;
  d.fillRect(0, top, screenW(), bottom - top, TFT_BLACK);

  int y = kTopH + 4;
  const uint8_t visible_lines = visibleDetailLines();
  for (uint8_t i = 0; i < visible_lines; ++i) {
    uint8_t line_index = g_detail_top + i;
    if (line_index >= g_detail_line_count) {
      break;
    }
    uint16_t color = isDetailLabel(g_detail_lines[line_index]) ? TFT_YELLOW : TFT_WHITE;
    d.setTextColor(color, TFT_BLACK);
    d.setCursor(4, y);
    if (line_index == 0) {
      d.print(g_entry.word);
      if (g_entry.phonetic.length() > 0) {
        String gap = g_entry.word + "  ";
        int16_t phonetic_x = 4 + d.textWidth(gap);
        if (g_ipa_font_loaded) {
          d.drawString(g_entry.phonetic, phonetic_x, y, &g_ipa_font);
        } else {
          d.setCursor(phonetic_x, y);
          d.print(g_entry.phonetic);
        }
      }
    } else {
      d.print(g_detail_lines[line_index]);
    }
    y += kLineH;
  }
}

static void drawDetailScreen() {
  auto &d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  drawTopBar("Detail");
  drawDetailBody();
  drawBottomBar("; Up  . Down  Esc Back");
}

static void drawError() {
  auto &d = M5Cardputer.Display;
  d.fillScreen(TFT_BLACK);
  drawTopBar("CardDic Error");
  d.setTextColor(TFT_ORANGE, TFT_BLACK);
  d.setCursor(5, 28);
  d.print(g_status);
  d.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  d.setCursor(5, 52);
  d.print("Need SD files:");
  d.setCursor(5, 66);
  d.print("/carddic/index.bin");
  d.setCursor(5, 80);
  d.print("/carddic/entries.dat");
  d.setCursor(5, 94);
  d.print("/carddic/prefix.bin");
  d.setCursor(5, 108);
  d.print("/carddic/ipa.bff");
}

static void redraw() {
  if (g_view == ViewMode::Search && g_input_append_redraw && !g_needs_redraw) {
    drawSearchInputAppend(g_input_append_char);
    g_input_append_redraw = false;
  }
  if (g_view == ViewMode::Search && g_input_delete_redraw && !g_needs_redraw) {
    drawSearchInputDelete(g_input_delete_pos);
    g_input_delete_redraw = false;
  }
  if (g_view == ViewMode::Search && g_input_needs_redraw && !g_needs_redraw) {
    drawSearchInput();
    g_input_needs_redraw = false;
  }
  if (g_view == ViewMode::Search && g_results_needs_redraw && !g_needs_redraw) {
    drawSearchResults();
    g_results_needs_redraw = false;
    return;
  }
  if (g_view == ViewMode::Detail && g_detail_body_needs_redraw && !g_needs_redraw) {
    drawDetailBody();
    g_detail_body_needs_redraw = false;
    return;
  }
  if (!g_needs_redraw) {
    return;
  }
  if (g_view == ViewMode::Search) {
    drawSearch();
  } else if (g_view == ViewMode::Detail) {
    drawDetailScreen();
  } else {
    drawError();
  }
  g_needs_redraw = false;
  g_input_needs_redraw = false;
  g_input_append_redraw = false;
  g_input_delete_redraw = false;
  g_detail_body_needs_redraw = false;
  g_results_needs_redraw = false;
}

static bool initStorage() {
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    showFatal("SD card not found");
    return false;
  }
  if (!fileExists(kIndexPath)) {
    showFatal("Missing index.bin");
    return false;
  }
  if (!fileExists(kEntriesPath)) {
    showFatal("Missing entries.dat");
    return false;
  }
  if (!fileExists(kPrefixPath)) {
    showFatal("Missing prefix.bin");
    return false;
  }
  if (!fileExists(kIpaFontPath)) {
    showFatal("Missing ipa.bff");
    return false;
  }

  g_index_file = SD.open(kIndexPath, FILE_READ);
  if (!g_index_file || g_index_file.read(reinterpret_cast<uint8_t *>(&g_header), sizeof(g_header)) != sizeof(g_header)) {
    showFatal("Bad index header");
    return false;
  }

  if (g_header.magic != kIndexMagic || g_header.version != kIndexVersion ||
      g_header.record_size != sizeof(IndexRecord) || g_header.count == 0) {
    showFatal("Unsupported index");
    return false;
  }

  PrefixHeader prefix_header = {};
  g_prefix_file = SD.open(kPrefixPath, FILE_READ);
  if (!g_prefix_file ||
      g_prefix_file.read(reinterpret_cast<uint8_t *>(&prefix_header), sizeof(prefix_header)) !=
          sizeof(prefix_header)) {
    showFatal("Bad prefix header");
    return false;
  }
  if (prefix_header.magic != kPrefixMagic || prefix_header.version != kIndexVersion ||
      prefix_header.record_size != sizeof(PrefixRecord)) {
    showFatal("Unsupported prefix");
    return false;
  }

  g_ipa_font_file = SD.open(kIpaFontPath, FILE_READ);
  if (!g_ipa_font_file || !g_ipa_font.loadFont(&g_ipa_font_data)) {
    showFatal("Cannot load ipa.bff");
    return false;
  }
  g_ipa_font_loaded = true;

  return true;
}

static void handleSearchKeys(const Keyboard_Class::KeysState &keys) {
  bool changed = false;
  bool clear = keys.ctrl || keys.fn || keys.opt || keys.alt;
  g_last_key_ms = millis();
  g_search_active = false;
  for (auto c : keys.word) {
    if (c == '`' || c == '~') {
      clear = true;
    } else if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '\'') {
      if (g_query.length() < 31) {
        char typed = static_cast<char>(tolower(c));
        g_query += typed;
        g_input_append_char = typed;
        g_input_append_redraw = true;
        changed = true;
      }
    }
  }
  if (keys.del && g_query.length() > 0) {
    g_input_delete_pos = g_query.length() - 1;
    g_query.remove(g_query.length() - 1);
    g_input_delete_redraw = true;
    changed = true;
  }
  if (clear && g_query.length() > 0) {
    g_query = "";
    g_result_count = 0;
    g_selected = 0;
    g_status = "";
    g_search_pending = false;
    g_search_active = false;
    changed = true;
    g_results_needs_redraw = true;
  }
  if (navDown(keys) && g_result_count > 0) {
    g_selected = (g_selected + 1) % g_result_count;
    g_results_needs_redraw = true;
  }
  if (navUp(keys) && g_result_count > 0) {
    g_selected = (g_selected == 0) ? g_result_count - 1 : g_selected - 1;
    g_results_needs_redraw = true;
  }
  if (keys.enter && g_result_count > 0) {
    if (loadEntry(g_results[g_selected])) {
      buildDetailLines();
      g_view = ViewMode::Detail;
      g_needs_redraw = true;
    }
  }
  if (changed) {
    g_search_pending = g_query.length() > 0;
    g_search_active = false;
    g_last_input_ms = millis();
    if (g_search_pending) {
      g_status = "";
    } else {
      g_result_count = 0;
      g_selected = 0;
      g_status = "";
      g_results_needs_redraw = true;
    }
    if (clear) {
      g_input_needs_redraw = true;
    }
  }
}

static void handleDetailKeys(const Keyboard_Class::KeysState &keys) {
  bool back = keys.del || keys.ctrl || keys.fn || keys.opt || keys.alt;
  for (auto c : keys.word) {
    if (c == '`' || c == '~') {
      back = true;
    }
  }
  if (back) {
    g_view = ViewMode::Search;
    g_needs_redraw = true;
    return;
  }
  if (navUp(keys) && g_detail_top > 0) {
    --g_detail_top;
    g_detail_body_needs_redraw = true;
  }
  if (navDown(keys) && g_detail_top + visibleDetailLines() < g_detail_line_count) {
    ++g_detail_top;
    g_detail_body_needs_redraw = true;
  }
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  Serial.begin(115200);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextWrap(false);
  M5Cardputer.Display.setTextDatum(top_left);
  M5Cardputer.Display.setFont(&fonts::efontCN_16);
  M5Cardputer.Display.fillScreen(TFT_BLACK);

  initStorage();
  redraw();
}

void loop() {
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    Keyboard_Class::KeysState keys = M5Cardputer.Keyboard.keysState();
    if (g_view == ViewMode::Search) {
      handleSearchKeys(keys);
    } else if (g_view == ViewMode::Detail) {
      handleDetailKeys(keys);
    }
  }

  uint32_t now = millis();
  bool input_idle = now - g_last_input_ms >= kSearchDelayMs && now - g_last_key_ms >= kSearchDelayMs;
  if (g_view == ViewMode::Search && g_search_pending && input_idle) {
    g_search_pending = false;
    beginSearch();
  }
  if (g_view == ViewMode::Search && input_idle) {
    searchStep();
  }
  redraw();
  delay(10);
}
