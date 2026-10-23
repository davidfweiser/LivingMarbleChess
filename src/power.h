// What the operating system says about saving power.
#pragma once

namespace power {
// The user asked the system to save power: Windows battery saver (energy saver), or on
// Linux the power-saver profile (ACPI platform profile "low-power").
bool saverOn();
}  // namespace power
