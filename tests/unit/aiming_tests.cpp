#include "ic2d/aiming.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

int aiming_failures = 0;

void aim_expect(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++aiming_failures;
    }
}

[[nodiscard]] bool near(const float left, const float right, const float tolerance = 0.001F) {
    return std::abs(left - right) < tolerance;
}

[[nodiscard]] float degrees_between(const ic2d::Vec2 left, const ic2d::Vec2 right) {
    const float cross = left.x * right.y - left.y * right.x;
    const float dot = left.x * right.x + left.y * right.y;
    return std::abs(std::atan2(cross, dot)) * 180.0F / 3.14159265358979F;
}

[[nodiscard]] ic2d::AimingInputs at_origin() {
    return {
        .actor_position = {0.0F, 0.0F, 0.0F},
        .pointer_active = false,
        .pointer_world_point = std::nullopt,
        .stick_world = {0.0F, 0.0F},
        .delta_seconds = 1.0F / 60.0F,
        .direct = false,
    };
}

} // namespace

int main() {
    const std::span<const ic2d::AimTarget> no_targets{};

    // A pointer is absolute: it must land exactly where it points, with no
    // smoothing, because the player already moved their hand there.
    {
        ic2d::Aiming aiming;
        ic2d::AimingInputs inputs = at_origin();
        inputs.pointer_active = true;
        inputs.pointer_world_point = ic2d::Vec3{100.0F, 0.0F, 0.0F};
        const ic2d::AimingSnapshot& snapshot = aiming.resolve(inputs, no_targets);
        aim_expect(snapshot.aiming && snapshot.pointer_source,
                   "A pointer away from the actor must count as aiming.");
        aim_expect(near(snapshot.direction.x, 1.0F) && near(snapshot.direction.y, 0.0F),
                   "Pointer aim must resolve exactly toward the pointer.");
        aim_expect(!snapshot.turning, "Pointer aim must never be rate limited.");
        aim_expect(near(snapshot.distance, 100.0F),
                   "Pointer aim distance must be the ground distance to the pointer.");
        aim_expect(near(snapshot.aim_point.x, 100.0F) && near(snapshot.aim_point.z, 0.0F),
                   "The aim point must be the pointer's ground position.");

        // The muzzle, not the actor centre, is where a shot leaves. Crosshair
        // and projectile agree only if this is what combat is given.
        aim_expect(near(snapshot.origin.x, aiming.config().muzzle.forward) &&
                       near(snapshot.origin.y, aiming.config().muzzle.height) &&
                       near(snapshot.origin.z, 0.0F),
                   "The origin must sit forward of the actor along the aim, at muzzle height.");

        // The projectile spawn resolves its origin through the same function,
        // which is the only reason the two can never disagree.
        const ic2d::Vec3 spawn =
            ic2d::muzzle_origin({0.0F, 0.0F, 0.0F}, snapshot.direction, aiming.config().muzzle);
        aim_expect(near(spawn.x, snapshot.origin.x) && near(spawn.y, snapshot.origin.y) &&
                       near(spawn.z, snapshot.origin.z),
                   "A shot must leave from exactly the origin the aim resolved.");
        const ic2d::Vec3 degenerate =
            ic2d::muzzle_origin({5.0F, 0.0F, 5.0F}, {0.0F, 0.0F}, aiming.config().muzzle);
        aim_expect(near(degenerate.x, 5.0F) && near(degenerate.z, 5.0F) &&
                       near(degenerate.y, aiming.config().muzzle.height),
                   "A zero aim direction must not divide by zero.");
    }

    // A pointer resting on the actor has no direction to give, so the aim holds
    // instead of spinning on sub-pixel noise.
    {
        ic2d::Aiming aiming;
        ic2d::AimingInputs inputs = at_origin();
        inputs.pointer_active = true;
        inputs.pointer_world_point = ic2d::Vec3{100.0F, 0.0F, 0.0F};
        static_cast<void>(aiming.resolve(inputs, no_targets));
        inputs.pointer_world_point = ic2d::Vec3{0.4F, 0.0F, -0.2F};
        const ic2d::AimingSnapshot& held = aiming.resolve(inputs, no_targets);
        aim_expect(!held.aiming, "A pointer on the actor must not count as aiming.");
        aim_expect(near(held.direction.x, 1.0F) && near(held.direction.y, 0.0F),
                   "A pointer on the actor must hold the previous direction.");
    }

    // A stick at rest must not drift the aim.
    {
        ic2d::Aiming aiming;
        ic2d::AimingInputs inputs = at_origin();
        inputs.stick_world = {0.10F, 0.05F};
        const ic2d::AimingSnapshot& snapshot = aiming.resolve(inputs, no_targets);
        aim_expect(!snapshot.aiming, "A stick inside the dead zone must not aim.");
        aim_expect(near(snapshot.direction.x, 0.0F) && near(snapshot.direction.y, 1.0F),
                   "A resting stick must hold the default facing.");
    }

    // A stick is a rate control. Full deflection must turn quickly but not
    // instantly, and it must arrive at exactly what was asked for.
    {
        ic2d::Aiming aiming;
        ic2d::AimingInputs inputs = at_origin();
        inputs.stick_world = {1.0F, 0.0F}; // 90 degrees from the default +Z.
        const ic2d::AimingSnapshot& first = aiming.resolve(inputs, no_targets);
        aim_expect(first.aiming && first.turning,
                   "A full stick deflection across 90 degrees must be rate limited.");
        const float turned = degrees_between({0.0F, 1.0F}, first.direction);
        const float allowed =
            aiming.config().stick_turn_degrees_per_second * (1.0F / 60.0F) + 0.01F;
        aim_expect(turned > 0.0F && turned <= allowed,
                   "One frame of turning must not exceed the configured rate.");

        int frames = 1;
        while (aiming.snapshot().turning && frames < 240) {
            static_cast<void>(aiming.resolve(inputs, no_targets));
            ++frames;
        }
        aim_expect(frames < 240, "A held stick must reach its requested direction.");
        aim_expect(near(aiming.snapshot().direction.x, 1.0F, 0.01F) &&
                       near(aiming.snapshot().direction.y, 0.0F, 0.01F),
                   "Turning must settle exactly on the requested direction.");
    }

    // Partial deflection turns proportionally slower, which is what makes fine
    // adjustment possible at all.
    {
        ic2d::Aiming full;
        ic2d::Aiming partial;
        ic2d::AimingInputs strong = at_origin();
        strong.stick_world = {1.0F, 0.0F};
        ic2d::AimingInputs light = at_origin();
        light.stick_world = {0.45F, 0.0F};
        const float fast =
            degrees_between({0.0F, 1.0F}, full.resolve(strong, no_targets).direction);
        const float slow =
            degrees_between({0.0F, 1.0F}, partial.resolve(light, no_targets).direction);
        aim_expect(slow > 0.0F && slow < fast,
                   "A light stick push must turn slower than a full one.");
    }

    // Assist is a nudge with a bound, not a lock.
    {
        const std::array<ic2d::AimTarget, 1> targets{
            ic2d::AimTarget{.actor = {77}, .position = {100.0F, 0.0F, 12.0F}},
        };
        ic2d::Aiming aiming;
        ic2d::AimingInputs inputs = at_origin();
        inputs.pointer_active = true;
        inputs.pointer_world_point = ic2d::Vec3{100.0F, 0.0F, 0.0F};
        const ic2d::AimingSnapshot& snapshot = aiming.resolve(inputs, targets);
        aim_expect(snapshot.assisted_target.has_value() && snapshot.assisted_target->value == 77,
                   "A target inside the cone must be assisted toward.");

        const ic2d::Vec2 raw{1.0F, 0.0F};
        const ic2d::Vec2 exact = {100.0F / std::sqrt(100.0F * 100.0F + 12.0F * 12.0F),
                                  12.0F / std::sqrt(100.0F * 100.0F + 12.0F * 12.0F)};
        const float to_raw = degrees_between(raw, snapshot.direction);
        const float to_exact = degrees_between(exact, snapshot.direction);
        aim_expect(to_raw > 0.0F, "Assist must actually move the aim.");
        aim_expect(to_exact > 0.0F,
                   "Assist must not snap the aim onto the target: the player keeps authority.");
        aim_expect(near(snapshot.distance, std::sqrt(100.0F * 100.0F + 12.0F * 12.0F), 0.01F),
                   "An assisted aim point must sit at the target's range.");
    }

    // A target outside the cone or beyond range must be ignored entirely.
    {
        const std::array<ic2d::AimTarget, 2> targets{
            ic2d::AimTarget{.actor = {1}, .position = {100.0F, 0.0F, 90.0F}}, // wide
            ic2d::AimTarget{.actor = {2}, .position = {5000.0F, 0.0F, 0.0F}}, // far
        };
        ic2d::Aiming aiming;
        ic2d::AimingInputs inputs = at_origin();
        inputs.pointer_active = true;
        inputs.pointer_world_point = ic2d::Vec3{100.0F, 0.0F, 0.0F};
        const ic2d::AimingSnapshot& snapshot = aiming.resolve(inputs, targets);
        aim_expect(!snapshot.assisted_target.has_value(),
                   "Targets outside the cone or beyond range must not be assisted toward.");
        aim_expect(near(snapshot.direction.x, 1.0F) && near(snapshot.direction.y, 0.0F),
                   "An unassisted aim must equal the raw request.");
    }

    // Automation states an exact direction. Smoothing or assist there would
    // make a recorded run depend on frame pacing and on nearby enemies, so a
    // direct frame must reproduce the request bit for bit.
    {
        const std::array<ic2d::AimTarget, 1> targets{
            ic2d::AimTarget{.actor = {9}, .position = {100.0F, 0.0F, 12.0F}},
        };
        ic2d::Aiming aiming;
        ic2d::AimingInputs inputs = at_origin();
        inputs.direct = true;
        inputs.stick_world = {1.0F, 0.0F};
        const ic2d::AimingSnapshot& snapshot = aiming.resolve(inputs, targets);
        aim_expect(near(snapshot.direction.x, 1.0F) && near(snapshot.direction.y, 0.0F),
                   "A direct frame must adopt the requested direction exactly.");
        aim_expect(!snapshot.turning && !snapshot.assisted_target.has_value(),
                   "A direct frame must apply neither smoothing nor assist.");

        // Frame pacing must not change the outcome of a direct frame.
        ic2d::Aiming slow;
        ic2d::AimingInputs long_frame = inputs;
        long_frame.delta_seconds = 0.25F;
        const ic2d::AimingSnapshot& paced = slow.resolve(long_frame, targets);
        aim_expect(near(paced.direction.x, snapshot.direction.x) &&
                       near(paced.direction.y, snapshot.direction.y),
                   "A direct frame must not depend on the frame duration.");
    }

    // A bad sample must hold the last good aim rather than snapping.
    {
        ic2d::Aiming aiming;
        ic2d::AimingInputs inputs = at_origin();
        inputs.pointer_active = true;
        inputs.pointer_world_point = ic2d::Vec3{0.0F, 0.0F, -100.0F};
        static_cast<void>(aiming.resolve(inputs, no_targets));

        ic2d::AimingInputs broken = at_origin();
        broken.actor_position = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        const ic2d::AimingSnapshot& held = aiming.resolve(broken, no_targets);
        aim_expect(near(held.direction.x, 0.0F) && near(held.direction.y, -1.0F),
                   "An invalid frame must hold the previous direction.");
        aim_expect(!held.aiming, "An invalid frame must not report an aim.");
    }

    if (aiming_failures == 0) {
        std::cout << "Aiming tests passed.\n";
    }
    return aiming_failures == 0 ? 0 : 1;
}
