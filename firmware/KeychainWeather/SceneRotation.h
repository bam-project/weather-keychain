//==============================================================================
//  Date:     11/07/2026
//  Program:  Weather Keychain
//  File:     SceneRotation.h
//
//  Purpose:  Select weather artwork using independent non-repeating shuffle bags.
//==============================================================================

#pragma once
#include <stdint.h>
// Independent shuffle bags retain progress when the weather changes.
struct SceneRotation {
  uint8_t bag[8][19]{};
  uint8_t cursor[8]{};
  bool ready[8]{};
  int last[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  // Supply the random-number source used to shuffle the scene indices.
  template <class Random>
  //------------------------------------------------------------------------------
  // next
  // Purpose:  Return the next shuffled scene for a weather group.
  // Args:     group - weather artwork group index
  //           random - function that supplies a random unsigned integer
  // Returns:  global artwork index, or -1 for an invalid group.
  //------------------------------------------------------------------------------
  int next(int group, Random random) {
    if (group < 0 || group >= 8) {
      return -1;
    }
    const int count = group < 4 ? 19 : 18;
    const int offset = group < 4 ? group * 19 : 76 + (group - 4) * 18;
    if (!ready[group] || cursor[group] == count) {
      // Refill the bag only after every scene in this group has appeared.
      for (int i = 0; i < count; i++) {
        bag[group][i] = i;
      }
      // Fisher-Yates shuffle: swap each entry with a random earlier entry.
      for (int i = count - 1; i > 0; i--) {
        int j = random() % (i + 1);
        auto previousValue = bag[group][i];
        bag[group][i] = bag[group][j];
        bag[group][j] = previousValue;
      }
      // Prevent the first scene of a new bag from repeating the previous scene.
      if (bag[group][0] == last[group]) {
        auto previousValue = bag[group][0];
        bag[group][0] = bag[group][1];
        bag[group][1] = previousValue;
      }
      cursor[group] = 0;
      ready[group] = true;
    }
    last[group] = bag[group][cursor[group]++];
    return offset + last[group];
  }
};
