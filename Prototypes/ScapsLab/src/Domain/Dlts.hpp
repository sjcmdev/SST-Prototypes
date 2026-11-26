#pragma once
#include <scaps_core/transient.hpp>
#include <sst/physics/dlts.hpp>
#include "Domain/Packed.hpp"
#include <memory>

namespace SST::ScapsLab {
enum class DltsSeries { Temperature, PulseWidth, Depth, DoublePulse };
enum class DltsReadout { Frozen, Demodulated };
struct DltsConfig {
    DltsSeries series=DltsSeries::Temperature;
    DltsReadout readout=DltsReadout::Frozen;
    double reverse_V=-.2, pulse_V=0, second_pulse_V=.02, pulse_width_s=1e-5;
    double first_sample_s=1e-6, last_sample_s=.01, initial_step_s=1e-10, max_step_s=1e-5;
    int samples=129, max_steps=200000;
    double ac_amplitude_V=1e-3, generation_multiplier=1;
    bool minority=true, poole_frenkel=false;
    sst::physics::dlts::FilterKind filter=sst::physics::dlts::FilterKind::Boxcar;
    double gate_start_s=1e-4, gate_ratio=2, window_ratio=2;
    int windows=5;
    double rate_min_per_s=1, rate_max_per_s=1e6, alpha=1e-3, polarity=1;
    sst::physics::dlts::AlphaMethod alpha_method=sst::physics::dlts::AlphaMethod::Gcv;
    int grid_size=81, regularization_order=2;
    double dos_mass_m0=.1, velocity_mass_m0=.1, degeneracy_factor=1;
    double donor_density_m3=1e21, profiling_eps_r=11.7;
    double fermi_minus_trap_reverse_eV=0, fermi_minus_trap_pulse_eV=0;
    double capture_density_m3=1e21, capture_velocity_m_s=1e5;
};
struct DltsTrace {
    std::string id, status="converged", message;
    double temperature_K=0, pulse_width_s=0, reverse_V=0, pulse_V=0;
    double readout_resolution_F=0; // requested relative DC residual target times equilibrium C; a conservative inference screen, not a certified error bar
    double baseline_F=0, equilibrium_F=0, depth_m=0, pulse_depth_m=0, trap_density_m3=0;
    bool density_valid=false;
    Column time_s, capacitance_F, current_A_m2;
    Column reference_capacitance_F; // DDLTS: paired pulse, on the identical time grid.
    std::vector<double> signal_F, amplitude_F;
    std::optional<sst::physics::dlts::LaplaceResult> laplace;
};
struct CaptureFit {double rate_per_s=0, sigma_m2=0, amplitude_F=0, rms_F=0;};
struct DltsData {
    std::vector<std::shared_ptr<const DltsTrace>> traces;
    std::vector<sst::physics::dlts::Spectrum> spectra;
    std::optional<sst::physics::dlts::ArrheniusResult> arrhenius;
    std::optional<CaptureFit> capture;
    std::vector<std::string> warnings;
    bool cancelled=false;
};
void ValidateDlts(const DltsConfig&);
std::vector<sst::physics::dlts::Filter> DltsFilters(const DltsConfig&);
CaptureFit FitCapture(const std::vector<double>& width_s,const std::vector<double>& amplitude_F,double density_m3,double velocity_m_s);
void AnalyzeDlts(DltsData&,const DltsConfig&,bool laplace);
// File/Python edge codecs. Simulation, processing and UI use the structs above.
nlohmann::json DltsConfigJson(const DltsConfig&);
DltsConfig ReadDltsConfig(const nlohmann::json&);
nlohmann::json DltsDataJson(const DltsData&);
std::shared_ptr<const DltsData> ReadDltsData(const nlohmann::json&);
}
