// ПДА «Горизонт» — экраны чатов, контактов и открытых диалогов.
#ifdef HORIZON_UI

#include "horizon/HorizonConfig.h"
#include "horizon/HorizonStrings.h"
#include "horizon/HorizonTheme.h"
#include "horizon/HorizonUI.h"
#include "horizon/HorizonView.h"
#include "graphics/common/ViewController.h"
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace HorizonStrings;

namespace {
constexpr int16_t kStatusH = 22;
constexpr int16_t kHeaderH = 40;
constexpr int16_t kInputH = 48;
} // namespace

// ---------------- чат ----------------

void HorizonUI::showChatChannel(uint8_t ch)
{
    chatIsChannel = true;
    chatCh = ch;
    chatPeer = 0;
    buildChat();
}

void HorizonUI::showChatDm(uint32_t peer)
{
    chatIsChannel = false;
    chatPeer = peer;
    buildChat();
}

void HorizonUI::buildChat(void)
{
    clearScreen(Screen::CHAT);
    HorizonConfig &cfg = HorizonConfig::instance();

    char title[64];
    char sub[80];
    if (chatIsChannel) {
        const char *kind = "Канал";
        if (chatCh == cfg.generalCh)
            kind = "Общий чат";
        else if (chatCh == cfg.groupCh)
            kind = "Чат группировки";
        else if (chatCh == cfg.announceCh)
            kind = "Объявления";
        snprintf(title, sizeof(title), "%s", kind);
        const char *lock = view->channelIsLocked(chatCh) ? kGlyphLock : kGlyphUnlock;
        snprintf(sub, sizeof(sub), "%s · %s", view->channelLabel(chatCh), lock);
    } else {
        snprintf(title, sizeof(title), "%s", view->nodeLongName(chatPeer));
        const NodeRecord *r = view->getNodes().find(chatPeer);
        if (r) {
            char lh[24];
            fmtLastHeard(view, r->lastHeard, lh, sizeof(lh));
            snprintf(sub, sizeof(sub), "%s · %s", view->nodeShortName(chatPeer), lh);
        } else {
            snprintf(sub, sizeof(sub), "0x%08X", (unsigned)chatPeer);
        }
    }

    addStatusBar(scr);
    addNavHeader(scr, title, sub);

    int16_t top = kStatusH + kHeaderH + 2;
    chatList = lv_obj_create(scr);
    HorizonTheme::styleList(chatList);
    lv_obj_set_pos(chatList, 0, top);
    lv_obj_set_size(chatList, W(), H() - top - kInputH - 4);
    lv_obj_set_flex_flow(chatList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(chatList, 4, LV_PART_MAIN);

    // Строка ввода
    lv_obj_t *bar = lv_obj_create(scr);
    HorizonTheme::styleHeader(bar);
    lv_obj_set_size(bar, lv_pct(100), kInputH);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 6, LV_PART_MAIN);

    chatInput = lv_textarea_create(bar);
    HorizonTheme::styleField(chatInput);
    lv_textarea_set_one_line(chatInput, true);
    lv_textarea_set_max_length(chatInput, 200);
    lv_textarea_set_placeholder_text(chatInput, "Сообщение...");
    lv_obj_set_flex_grow(chatInput, 1);
    lv_obj_set_height(chatInput, 38);
    lv_obj_add_event_cb(chatInput, cbInputFocus, LV_EVENT_FOCUSED, this);
    lv_obj_add_event_cb(chatInput, cbInputFocus, LV_EVENT_DEFOCUSED, this);

    chatSendBtn = lv_btn_create(bar);
    HorizonTheme::styleButton(chatSendBtn, true);
    lv_obj_set_size(chatSendBtn, 76, 38);
    lv_obj_t *sl = lv_label_create(chatSendBtn);
    lv_label_set_text(sl, ">>");
    lv_obj_center(sl);
    lv_obj_add_event_cb(chatSendBtn, cbSend, LV_EVENT_CLICKED, this);

    rebuildChatList();
}

void HorizonUI::rebuildChatList(void)
{
    if (current != Screen::CHAT || !chatList || !lv_obj_is_valid(chatList))
        return;
    HorizonStore &st = view->getStore();
    HorizonConvo &c = chatIsChannel ? st.channelConvo(chatCh) : st.dmConvo(chatPeer);
    lv_obj_clean(chatList);
    if (c.messages.empty()) {
        lv_obj_t *e = lv_label_create(chatList);
        lv_label_set_text(e, "Пока тихо в эфире...\nНапиши первым.");
        lv_obj_set_style_text_color(e, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);
        lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_width(e, lv_pct(100));
    } else {
        for (auto &m : c.messages)
            appendChatBubble(m.from, m.text.c_str(), m.time, m.outgoing);
        // в конец
        uint32_t n = lv_obj_get_child_count(chatList);
        if (n > 0) {
            lv_obj_update_layout(chatList);
            lv_obj_scroll_to_view(lv_obj_get_child(chatList, n - 1), LV_ANIM_OFF);
        }
    }
    st.markRead(c);
    refreshStatus();
    refreshMenuBadges();
}

void HorizonUI::appendChatBubble(uint32_t from, const char *text, uint32_t time, bool outgoing)
{
    if (current != Screen::CHAT || !chatList || !lv_obj_is_valid(chatList))
        return;
    const HorizonPalette &p = HorizonTheme::current();

    // убрать плейсхолдер
    if (lv_obj_get_child_count(chatList) == 1) {
        lv_obj_t *only = lv_obj_get_child(chatList, 0);
        if (lv_obj_check_type(only, &lv_label_class))
            lv_obj_clean(chatList);
    }

    lv_obj_t *row = lv_obj_create(chatList);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    lv_obj_t *b = lv_obj_create(row);
    lv_obj_set_width(b, lv_pct(86));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(b, hzColor(outgoing ? p.outgoing : p.incoming), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(b, hzColor(outgoing ? p.accent : p.border), LV_PART_MAIN);
    lv_obj_set_style_border_width(b, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(b, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(b, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(b, 1, LV_PART_MAIN);

    char tm[8];
    fmtTime(time, tm, sizeof(tm));
    lv_obj_t *meta = lv_label_create(b);
    if (outgoing)
        lv_label_set_text_fmt(meta, "Вы · %s", tm);
    else if (chatIsChannel)
        lv_label_set_text_fmt(meta, "%s · %s", view->nodeLongName(from), tm);
    else
        lv_label_set_text_fmt(meta, "%s", tm);
    lv_obj_set_style_text_font(meta, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(meta, hzColor(outgoing ? p.accentText : p.textDim), LV_PART_MAIN);

    lv_obj_t *body = lv_label_create(b);
    lv_label_set_text(body, text ? text : "");
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_style_text_color(body, hzColor(outgoing ? p.accentText : p.text), LV_PART_MAIN);

    lv_obj_update_layout(chatList);
    lv_obj_scroll_to_view(row, LV_ANIM_OFF);
}

void HorizonUI::sendChatInput(void)
{
    if (!chatInput || !lv_obj_is_valid(chatInput))
        return;
    const char *txt = lv_textarea_get_text(chatInput);
    if (!txt || !*txt)
        return;
    if (chatIsChannel) {
        view->sendChannelMessage(chatCh, txt);
        appendChatBubble(view->getMyNodeNum(), txt, view->getActTime(), true);
        HorizonStore &st = view->getStore();
        st.markRead(st.channelConvo(chatCh));
    } else {
        view->sendDirectMessage(chatPeer, txt);
        appendChatBubble(view->getMyNodeNum(), txt, view->getActTime(), true);
        HorizonStore &st = view->getStore();
        st.markRead(st.dmConvo(chatPeer));
    }
    lv_textarea_set_text(chatInput, "");
    refreshStatus();
}

void HorizonUI::cbSend(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    self->sendChatInput();
}

void HorizonUI::cbInputFocus(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    if (code == LV_EVENT_FOCUSED)
        self->kbdShow(ta);
    else if (code == LV_EVENT_DEFOCUSED)
        self->kbdHide();
}

// ---------------- контакты ----------------

void HorizonUI::showContacts(void)
{
    buildContacts();
}

void HorizonUI::buildContacts(void)
{
    clearScreen(Screen::CONTACTS);
    addStatusBar(scr);
    addNavHeader(scr, "Контакты", "Узлы в mesh-сети");

    // Фильтр
    lv_obj_t *frow = lv_obj_create(scr);
    lv_obj_set_size(frow, lv_pct(100), 36);
    lv_obj_set_pos(frow, 0, kStatusH + kHeaderH);
    lv_obj_set_style_bg_opa(frow, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(frow, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(frow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(frow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(frow, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(frow, 8, LV_PART_MAIN);

    lv_obj_t *fAll = lv_btn_create(frow);
    HorizonTheme::styleButton(fAll, !contactsOnlineOnly);
    lv_obj_set_size(fAll, 90, 28);
    lv_obj_t *fal = lv_label_create(fAll);
    lv_label_set_text(fal, "Все");
    lv_obj_center(fal);
    lv_obj_set_user_data(fAll, (void *)(intptr_t)0);
    lv_obj_add_event_cb(fAll, cbFilter, LV_EVENT_CLICKED, this);

    lv_obj_t *fOn = lv_btn_create(frow);
    HorizonTheme::styleButton(fOn, contactsOnlineOnly);
    lv_obj_set_size(fOn, 110, 28);
    lv_obj_t *fol = lv_label_create(fOn);
    lv_label_set_text(fol, "В сети");
    lv_obj_center(fol);
    lv_obj_set_user_data(fOn, (void *)(intptr_t)1);
    lv_obj_add_event_cb(fOn, cbFilter, LV_EVENT_CLICKED, this);

    contactsCount = lv_label_create(frow);
    lv_obj_set_style_text_font(contactsCount, HorizonTheme::fontSmall(), LV_PART_MAIN);
    lv_obj_set_style_text_color(contactsCount, hzColor(HorizonTheme::current().textDim), LV_PART_MAIN);

    int16_t top = kStatusH + kHeaderH + 38;
    contactsList = lv_obj_create(scr);
    HorizonTheme::styleList(contactsList);
    lv_obj_set_pos(contactsList, 0, top);
    lv_obj_set_size(contactsList, lv_pct(100), H() - top - 2);
    lv_obj_set_flex_flow(contactsList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(contactsList, 4, LV_PART_MAIN);

    rebuildContactsList();
}

void HorizonUI::rebuildContactsList(void)
{
    if (current != Screen::CONTACTS || !contactsList || !lv_obj_is_valid(contactsList))
        return;
    const HorizonPalette &p = HorizonTheme::current();
    lv_obj_clean(contactsList);

    struct Row {
        uint32_t id;
        uint32_t lh;
    };
    std::vector<Row> rows;
    for (auto &kv : view->getNodes().records()) {
        if (kv.first == view->getMyNodeNum())
            continue;
        if (contactsOnlineOnly && !view->isNodeOnline(kv.first))
            continue;
        rows.push_back({kv.first, kv.second.lastHeard});
    }
    // свежие сверху
    for (size_t i = 0; i < rows.size(); i++) {
        for (size_t j = i + 1; j < rows.size(); j++) {
            if (rows[j].lh > rows[i].lh) {
                Row t = rows[i];
                rows[i] = rows[j];
                rows[j] = t;
            }
        }
    }

    if (contactsCount && lv_obj_is_valid(contactsCount))
        lv_label_set_text_fmt(contactsCount, "Всего: %u", (unsigned)rows.size());

    if (rows.empty()) {
        lv_obj_t *e = lv_label_create(contactsList);
        lv_label_set_text(e, contactsOnlineOnly ? "Никого нет в сети" : "Пока нет узлов");
        lv_obj_set_style_text_color(e, hzColor(p.textDim), LV_PART_MAIN);
        lv_obj_set_width(e, lv_pct(100));
        lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }

    for (auto &r : rows) {
        const NodeRecord *rec = view->getNodes().find(r.id);
        bool online = view->isNodeOnline(r.id);

        lv_obj_t *b = lv_btn_create(contactsList);
        HorizonTheme::styleButton(b, false);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_height(b, 54);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(b, 8, LV_PART_MAIN);

        // бейдж короткого имени
        lv_obj_t *badge = lv_obj_create(b);
        lv_obj_set_size(badge, 52, 38);
        lv_obj_set_style_bg_color(badge, hzColor(online ? p.ok : p.border), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(badge, 4, LV_PART_MAIN);
        lv_obj_set_style_border_width(badge, 0, LV_PART_MAIN);
        lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *bt = lv_label_create(badge);
        lv_label_set_text(bt, view->nodeShortName(r.id));
        lv_obj_set_style_text_font(bt, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(bt, lv_color_black(), LV_PART_MAIN);
        lv_obj_center(bt);

        lv_obj_t *col = lv_obj_create(b);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_height(col, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(col, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(col, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *nm = lv_label_create(col);
        lv_label_set_text(nm, view->nodeLongName(r.id));
        lv_obj_set_width(nm, lv_pct(100));

        char sub[72];
        char lh[24];
        fmtLastHeard(view, rec ? rec->lastHeard : 0, lh, sizeof(lh));
        if (rec && (rec->signalDisplay != NodeSignalDisplayKind::None || rec->rssi != 0))
            snprintf(sub, sizeof(sub), "%s · SNR %.1f", lh, rec->snr);
        else
            snprintf(sub, sizeof(sub), "%s", lh);
        lv_obj_t *sb = lv_label_create(col);
        lv_label_set_text(sb, sub);
        lv_obj_set_style_text_font(sb, HorizonTheme::fontSmall(), LV_PART_MAIN);
        lv_obj_set_style_text_color(sb, hzColor(p.textDim), LV_PART_MAIN);

        // непрочитанные лички
        const HorizonConvo *dm = view->getStore().findDmConvo(r.id);
        if (dm && dm->unread > 0) {
            lv_obj_t *ub = lv_label_create(b);
            lv_label_set_text_fmt(ub, "✉%u", (unsigned)dm->unread);
            lv_obj_set_style_bg_color(ub, hzColor(p.accent), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(ub, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_text_color(ub, hzColor(p.accentText), LV_PART_MAIN);
            lv_obj_set_style_text_font(ub, HorizonTheme::fontSmall(), LV_PART_MAIN);
            lv_obj_set_style_radius(ub, 8, LV_PART_MAIN);
            lv_obj_set_style_pad_hor(ub, 6, LV_PART_MAIN);
        }

        lv_obj_set_user_data(b, (void *)(uintptr_t)r.id);
        lv_obj_add_event_cb(b, cbContact, LV_EVENT_CLICKED, this);
    }
}

void HorizonUI::cbFilter(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    int id = (int)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    self->contactsOnlineOnly = (id == 1);
    self->buildContacts();
}

void HorizonUI::cbContact(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    uint32_t id = (uint32_t)(uintptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    self->showChatDm(id);
}

// ---------------- открытые чаты ----------------

void HorizonUI::showOpenChats(void)
{
    buildOpenChats();
}

void HorizonUI::buildOpenChats(void)
{
    clearScreen(Screen::OPENCHATS);
    const HorizonPalette &p = HorizonTheme::current();
    addStatusBar(scr);
    addNavHeader(scr, "Открытые чаты", "История личных сообщений");

    auto dms = view->getStore().dmList();

    int16_t top = kStatusH + kHeaderH + 2;
    int16_t bottomBar = dms.empty() ? 2 : 52;
    lv_obj_t *list = lv_obj_create(scr);
    HorizonTheme::styleList(list);
    lv_obj_set_pos(list, 0, top);
    lv_obj_set_size(list, lv_pct(100), H() - top - bottomBar);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 4, LV_PART_MAIN);

    if (dms.empty()) {
        lv_obj_t *e = lv_label_create(list);
        lv_label_set_text(e, "Личных чатов пока нет.\nОткрой Контакты и напиши сталкеру.");
        lv_obj_set_style_text_color(e, hzColor(p.textDim), LV_PART_MAIN);
        lv_obj_set_width(e, lv_pct(100));
        lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        return;
    }

    for (auto c : dms) {
        lv_obj_t *b = lv_btn_create(list);
        HorizonTheme::styleButton(b, c->unread > 0);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_height(b, LV_SIZE_CONTENT);
        lv_obj_set_style_min_height(b, 58, LV_PART_MAIN);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(b, 6, LV_PART_MAIN);

        lv_obj_t *row1 = lv_obj_create(b);
        lv_obj_set_width(row1, lv_pct(100));
        lv_obj_set_height(row1, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(row1, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(row1, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row1, 0, LV_PART_MAIN);
        lv_obj_set_flex_flow(row1, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row1, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(row1, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *nm = lv_label_create(row1);
        lv_label_set_text(nm, view->nodeLongName(c->peer));
        lv_obj_set_flex_grow(nm, 1);

        char tm[8];
        fmtTime(c->lastActivity, tm, sizeof(tm));
        lv_obj_t *t = lv_label_create(row1);
        lv_label_set_text(t, tm);
        lv_obj_set_style_text_font(t, HorizonTheme::fontSmall(), LV_PART_MAIN);

        const HorizonMessage &last = c->messages.back();
        char prev[96];
        strncpy(prev, last.text.c_str(), sizeof(prev) - 1);
        prev[sizeof(prev) - 1] = '\0';
        for (char *q = prev; *q; q++) {
            if (*q == '\n' || *q == '\r')
                *q = ' ';
        }
        lv_obj_t *pv = lv_label_create(b);
        lv_label_set_long_mode(pv, LV_LABEL_LONG_DOT);
        if (c->unread > 0)
            lv_label_set_text_fmt(pv, "✉%u  %s%s", (unsigned)c->unread, last.outgoing ? "Вы: " : "", prev);
        else
            lv_label_set_text_fmt(pv, "%s%s", last.outgoing ? "Вы: " : "", prev);
        lv_obj_set_style_text_font(pv, HorizonTheme::fontSmall(), LV_PART_MAIN);
        if (c->unread == 0)
            lv_obj_set_style_text_color(pv, hzColor(p.textDim), LV_PART_MAIN);
        lv_obj_set_width(pv, lv_pct(100));

        lv_obj_set_user_data(b, (void *)(uintptr_t)c->peer);
        lv_obj_add_event_cb(b, cbOpenChat, LV_EVENT_CLICKED, this);
    }

    // Кнопка очистки
    lv_obj_t *clr = lv_btn_create(scr);
    HorizonTheme::styleDangerButton(clr);
    lv_obj_set_size(clr, lv_pct(60), 36);
    lv_obj_align(clr, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_t *cl = lv_label_create(clr);
    lv_label_set_text(cl, "Очистить всё");
    lv_obj_center(cl);
    lv_obj_add_event_cb(clr, cbClearChats, LV_EVENT_CLICKED, this);
}

void HorizonUI::cbOpenChat(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    uint32_t id = (uint32_t)(uintptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e));
    self->showChatDm(id);
}

void HorizonUI::cbClearChats(lv_event_t *e)
{
    HorizonUI *self = (HorizonUI *)lv_event_get_user_data(e);
    self->askConfirm("Удалить ВСЮ историю сообщений на этом ПДА?", "Удалить", [self]() {
        self->view->getStore().clearAll();
        self->view->getController()->removeTextMessages(0, 0, 0);
        self->buildOpenChats();
        self->toast("История очищена");
    });
}

#endif // HORIZON_UI
