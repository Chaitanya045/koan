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

int Radius(Step step) {
  // The radius is the same in both modes.
  const TokenSet& tokens = CurrentTokens(false);
  switch (step) {
    case Step::kSm:
      return RadiusSm(tokens);
    case Step::kMd:
      return RadiusMd(tokens);
    case Step::kLg:
      return RadiusLg(tokens);
    case Step::kXl:
      return RadiusXl(tokens);
    case Step::k2xl:
      return RadiusXl(tokens) + 4;
  }
  return RadiusLg(tokens);
}

int StyleRadius(int zen_px, Step step) {
  return IsShadcnStyle() ? Radius(step) : zen_px;
}

int ScaleRadius(int zen_px) {
  if (!IsShadcnStyle()) {
    return zen_px;
  }
  if (zen_px <= 5) {
    return Radius(Step::kSm);
  }
  if (zen_px <= 9) {
    return Radius(Step::kMd);
  }
  if (zen_px <= 13) {
    return Radius(Step::kLg);
  }
  return Radius(zen_px <= 24 ? Step::kXl : Step::k2xl);
}

void AddObserver(Observer* observer) {
  GetState().observers.AddObserver(observer);
}

void RemoveObserver(Observer* observer) {
  GetState().observers.RemoveObserver(observer);
}

}  // namespace koan::look
