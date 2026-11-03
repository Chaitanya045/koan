// Copyright 2026 The Koan Authors
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "koan/browser/ui/color/koan_look.h"

#include <optional>
#include <utility>

#include "base/no_destructor.h"
#include "base/observer_list.h"

namespace koan::look {

namespace {

struct State {
  Look look;
  // The tokens of `look`, resolved on first use after a change.
  std::optional<TokenSet> light;
  std::optional<TokenSet> dark;
  base::ObserverList<Observer>::Unchecked observers;
};

State& GetState() {
  static base::NoDestructor<State> state;
  return *state;
}

// The radius scale is the same in both modes.
const TokenSet& Radii() {
  return CurrentTokens(false);
}

}  // namespace

const Look& CurrentLook() {
  return GetState().look;
}

bool SetLook(Look look) {
  State& state = GetState();
  if (state.look == look) {
    return false;
  }
  state.look = std::move(look);
  state.light.reset();
  state.dark.reset();
  for (Observer& observer : state.observers) {
    observer.OnLookChanged();
  }
  return true;
}

bool IsShadcnStyle() {
  return GetState().look.style == Style::kShadcn;
}

const TokenSet& CurrentTokens(bool dark) {
  State& state = GetState();
  std::optional<TokenSet>& cached = dark ? state.dark : state.light;
  if (!cached) {
    cached = ResolveTokens(state.look, dark);
  }
  return *cached;
}

int CurrentRadiusSm() {
  return RadiusSm(Radii());
}
int CurrentRadiusMd() {
  return RadiusMd(Radii());
}
int CurrentRadiusLg() {
  return RadiusLg(Radii());
}
int CurrentRadiusXl() {
  return RadiusXl(Radii());
}

void AddObserver(Observer* observer) {
  GetState().observers.AddObserver(observer);
}

void RemoveObserver(Observer* observer) {
  GetState().observers.RemoveObserver(observer);
}

}  // namespace koan::look
