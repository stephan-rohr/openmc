#include "openmc/secondary_uncorrelated.h"

#include <string>  // for string

#include <fmt/core.h>

#include "openmc/error.h"
#include "openmc/hdf5_interface.h"
#include "openmc/random_lcg.h"

namespace openmc {

//==============================================================================
// UncorrelatedAngleEnergy implementation
//==============================================================================

UncorrelatedAngleEnergy::UncorrelatedAngleEnergy(hid_t group)
{
  // Check if angle group is present & read
  if (object_exists(group, "angle")) {
    hid_t angle_group = open_group(group, "angle");
    angle_ = AngleDistribution{angle_group};
    close_group(angle_group);
  }

  // Check if energy group is present & read
  if (object_exists(group, "energy")) {
    hid_t energy_group = open_group(group, "energy");

    std::string type;
    read_attribute(energy_group, "type", type);
    using UPtrEDist = std::unique_ptr<EnergyDistribution>;
    if (type == "discrete_photon") {
      energy_ = UPtrEDist{new DiscretePhoton{energy_group}};
    } else if (type == "level") {
      energy_ = UPtrEDist{new LevelInelastic{energy_group}};
    } else if (type == "continuous") {
      energy_ = UPtrEDist{new ContinuousTabular{energy_group}};
    } else if (type == "maxwell") {
      energy_ = UPtrEDist{new MaxwellEnergy{energy_group}};
    } else if (type == "evaporation") {
      energy_ = UPtrEDist{new Evaporation{energy_group}};
    } else if (type == "watt") {
      energy_ = UPtrEDist{new WattEnergy{energy_group}};
    } else {
      warning(fmt::format("Energy distribution type '{}' not implemented.", type));
    }
    close_group(energy_group);
  }

}

void
UncorrelatedAngleEnergy::sample(double E_in, double& E_out, double& mu,
  uint64_t* seed) const
{
  // Sample cosine of scattering angle
  if (fission_) {
    // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<< REMOVE THIS <<<<<<<<<<<<<<<<<<<<<<<<<<<<<
    // For fission, the angle is not used, so just assign a dummy value
    mu = 1.0;
    // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<< REMOVE THIS <<<<<<<<<<<<<<<<<<<<<<<<<<<<<
  } else if (!angle_.empty()) {
    mu = angle_.sample(E_in, seed);
  } else {
    // no angle distribution given => assume isotropic for all energies
    mu = 2.0*prn(seed) - 1.0;
  }

  // Sample outgoing energy
  E_out = energy_->sample(E_in, seed);
}

UnifiedAngleEnergy UncorrelatedAngleEnergy::serialize() const
{
  // Determine size of angular distribution
  size_t bytes_angle = angle_.nbytes();

  size_t n = 8 + bytes_angle;
  if (energy_) {
    n += energy_->nbytes();
  }
  DataBuffer buffer(n);

  // Write locator for energy
  buffer.add(energy_ ? 8 + bytes_angle : 0);

  // Create buffer and serialize data
  angle_.serialize(buffer);
  if (energy_) energy_->serialize(buffer);

  return {AngleEnergyType::UNCORRELATED, std::move(buffer)};
}

AngleDistributionFlat UncorrelatedAngleEnergyFlat::angle() const
{
  return AngleDistributionFlat(data_ + 8);
}

EnergyDistributionFlat UncorrelatedAngleEnergyFlat::energy() const
{
  size_t offset = *reinterpret_cast<const size_t*>(data_);

  // Determine type
  auto type = static_cast<EnergyDistType>(*reinterpret_cast<const int*>(data_ + offset));
  return EnergyDistributionFlat(type, data_ + offset + 4);
}

void
UncorrelatedAngleEnergyFlat::sample(double E_in, double& E_out, double& mu, uint64_t* seed) const
{
  // Sample cosine of scattering angle
  mu = this->angle().sample(E_in, seed);

  // Sample outgoing energy
  E_out = this->energy().sample(E_in, seed);
}

} // namespace openmc
