#include <assert.h>
#include <stdio.h>

#include "TimerButton.h"

using Action = TimerButton::Action;

int main() {
  TimerButton button;
  assert(button.update(false, 0) == Action::None);
  assert(button.update(false, 30) == Action::None);

  assert(button.update(true, 100) == Action::None);
  assert(button.update(false, 110) == Action::None);
  assert(button.update(true, 120) == Action::None);
  assert(button.update(true, 149) == Action::None);
  assert(button.update(true, 150) == Action::None);
  assert(button.update(false, 200) == Action::None);
  assert(button.update(true, 210) == Action::None);
  assert(button.update(false, 220) == Action::None);
  assert(button.update(false, 249) == Action::None);
  assert(button.update(false, 250) == Action::Toggle);
  assert(button.update(false, 500) == Action::None);

  assert(button.update(true, 1000) == Action::None);
  assert(button.update(true, 1030) == Action::None);
  assert(button.update(true, 2529) == Action::None);
  assert(button.update(true, 2530) == Action::Reset);
  assert(button.update(true, 5000) == Action::None);
  assert(button.update(false, 5010) == Action::None);
  assert(button.update(false, 5040) == Action::None);

  assert(button.update(true, 6000) == Action::None);
  assert(button.update(true, 6030) == Action::None);
  assert(button.update(false, 6100) == Action::None);
  assert(button.update(false, 6130) == Action::Toggle);

  TimerButton heldAtBoot;
  assert(heldAtBoot.update(true, 0) == Action::None);
  assert(heldAtBoot.update(true, 5000) == Action::None);
  assert(heldAtBoot.update(false, 5010) == Action::None);
  assert(heldAtBoot.update(false, 5040) == Action::None);
  assert(heldAtBoot.update(true, 6000) == Action::None);
  assert(heldAtBoot.update(true, 6030) == Action::None);
  assert(heldAtBoot.update(false, 6100) == Action::None);
  assert(heldAtBoot.update(false, 6130) == Action::Toggle);

  TimerButton rollover;
  const uint32_t pressedAt = UINT32_MAX - 1000;
  assert(rollover.update(false, pressedAt - 100) == Action::None);
  assert(rollover.update(false, pressedAt - 70) == Action::None);
  assert(rollover.update(true, pressedAt) == Action::None);
  assert(rollover.update(true, pressedAt + 30) == Action::None);
  assert(rollover.update(true, pressedAt + 1529) == Action::None);
  assert(rollover.update(true, pressedAt + 1530) == Action::Reset);
  assert(rollover.update(false, pressedAt + 1600) == Action::None);
  assert(rollover.update(false, pressedAt + 1630) == Action::None);

  puts("PASS: button debounce, tap, exact hold threshold, startup and rollover");
}
