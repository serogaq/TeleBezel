#include "action_menu.h"
#include "diag.h"

static TbActionChosen s_chosen;
static void *s_context;
static uint8_t s_actions[TB_ACTIONS_MAX];
static int8_t s_picked = -1;

static void performed(ActionMenu *menu, const ActionMenuItem *item, void *context) {
  (void)menu; (void)context;
  s_picked = (int8_t)(uintptr_t)action_menu_item_get_action_data(item);
}

static void closed(ActionMenu *menu, const ActionMenuItem *item, void *context) {
  (void)item; (void)context;
  action_menu_hierarchy_destroy(action_menu_get_root_level(menu), NULL, NULL);
  const int8_t picked = s_picked;
  s_picked = -1;
  tb_diag_event("window_pop", "actions");
  if (picked >= 0 && s_chosen) { s_chosen(s_context, s_actions[picked]); }
}

void tb_actions_open(const char *const *labels, const uint8_t *actions, uint8_t count, TbActionChosen chosen, void *context) {
  if (count == 0) { return; }
  if (count > TB_ACTIONS_MAX) { count = TB_ACTIONS_MAX; }
  ActionMenuLevel *root = action_menu_level_create(count);
  if (!root) { return; }
  for (uint8_t index = 0; index < count; ++index) {
    s_actions[index] = actions[index];
    action_menu_level_add_action(root, labels[index], performed, (void *)(uintptr_t)index);
  }
  s_chosen = chosen;
  s_context = context;
  s_picked = -1;
  ActionMenuConfig config = {
    .root_level = root,
    .colors = {.background = GColorCobaltBlue, .foreground = GColorWhite},
    .align = ActionMenuAlignCenter,
    .did_close = closed,
  };
  action_menu_open(&config);
  tb_diag_event("window_push", "actions");
}
