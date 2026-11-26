#include "Workflow.hpp"
#include <scaps_core/transient.hpp>
#include <scaps_core/../../src/parallel.hpp>
#include <numbers>
#include <numeric>

namespace SST::ScapsLab {
namespace dlts=sst::physics::dlts;
void ValidateDlts(const DltsConfig& c) {
    for(double x:{c.pulse_width_s,c.first_sample_s,c.last_sample_s,c.initial_step_s,c.max_step_s,c.ac_amplitude_V,c.gate_start_s,c.gate_ratio,c.window_ratio,c.rate_min_per_s,c.rate_max_per_s,c.dos_mass_m0,c.velocity_mass_m0,c.degeneracy_factor,c.donor_density_m3,c.profiling_eps_r,c.capture_density_m3,c.capture_velocity_m_s})
        if(!std::isfinite(x) || x<=0)throw std::invalid_argument("DLTS times, rates and material conventions must be finite and positive");
    if(!std::isfinite(c.fermi_minus_trap_reverse_eV) || !std::isfinite(c.fermi_minus_trap_pulse_eV) || c.fermi_minus_trap_reverse_eV<0 || c.fermi_minus_trap_pulse_eV<0)throw std::invalid_argument("DLTS lambda energies must be finite and nonnegative");
    for(double x:{c.reverse_V,c.pulse_V,c.second_pulse_V})RequireParameter("voltage_V",x);
    if(int(c.series)<0 || int(c.series)>3 || int(c.readout)<0 || int(c.readout)>1 || int(c.filter)<0 || int(c.filter)>4 || int(c.alpha_method)<0 || int(c.alpha_method)>2)throw std::invalid_argument("Unknown DLTS protocol/readout/processing method");
    if(c.samples<17 || c.samples>4097 || c.max_steps<100 || c.max_steps>2000000 || c.windows<1 || c.windows>16 || c.first_sample_s>=c.last_sample_s || c.initial_step_s>c.max_step_s || c.gate_ratio<=1 || c.window_ratio<1 || c.gate_start_s<c.first_sample_s || c.gate_start_s*std::pow(c.window_ratio,c.windows-1)*c.gate_ratio>c.last_sample_s || c.rate_min_per_s>=c.rate_max_per_s || c.grid_size<11 || c.grid_size>201 || c.regularization_order<0 || c.regularization_order>2 || !std::isfinite(c.alpha) || c.alpha<0 || (c.polarity!=1 && c.polarity!=-1) || !std::isfinite(c.generation_multiplier) || c.generation_multiplier<=0 || c.generation_multiplier>1e6)
        throw std::invalid_argument("Invalid DLTS sampling, gate family, regularization or optical pulse");
}
std::vector<dlts::Filter> DltsFilters(const DltsConfig& c) {
    std::vector<dlts::Filter> filters;
    for(int i=0;i<c.windows;++i) {
        const double t=c.gate_start_s*std::pow(c.window_ratio,i),end=t*c.gate_ratio;
        dlts::Filter f;f.kind=c.filter;f.id="window-"+std::to_string(i);
        if(c.filter==dlts::FilterKind::Boxcar)f.gates={{t,t,1},{end,end,-1}};
        else if(c.filter==dlts::FilterKind::MultiGate)f.gates={{t,t*1.1,1},{end*.9,end,-1}};
        else if(c.filter==dlts::FilterKind::Exponential){f.delay_s=t;f.duration_s=end-t;f.tau_fast_s=(end-t)/8;f.tau_slow_s=(end-t)/2;}
        else {f.delay_s=t;f.duration_s=end-t;}
        filters.push_back(std::move(f));
    }
    return filters;
}
CaptureFit FitCapture(const std::vector<double>& t,const std::vector<double>& a,double n,double v) {
    if(t.size()!=a.size() || t.size()<4 || !(n>0) || !(v>0))throw std::invalid_argument("Capture fit needs four pulse widths and explicit n and v_th");
    for(std::size_t i=0;i<t.size();++i)if(!std::isfinite(t[i]) || t[i]<=0 || !std::isfinite(a[i]) || (i && t[i]<=t[i-1]))throw std::invalid_argument("Capture widths must increase and amplitudes must be finite");
    const auto trial=[&](double logRate) {
        const double rate=std::exp(logRate);double xx=0,xy=0;
        for(std::size_t i=0;i<t.size();++i){const double x=-std::expm1(-rate*t[i]);xx+=x*x;xy+=x*a[i];}
        const double amplitude=xy/xx;double error=0;
        for(std::size_t i=0;i<t.size();++i)error+=std::pow(a[i]-amplitude*(-std::expm1(-rate*t[i])),2);
        return std::pair{error,amplitude};
    };
    double lo=std::log(.001/t.back()),hi=std::log(1000/t.front());
    for(int i=0;i<160;++i){const double l=lo+(hi-lo)/3,r=hi-(hi-lo)/3;if(trial(l).first<trial(r).first)hi=r;else lo=l;}
    const double rate=std::exp((lo+hi)/2);const auto [error,amplitude]=trial((lo+hi)/2);
    if(rate*t.front()>5 || rate*t.back()<.2 || std::abs(amplitude)<1e-30)throw std::invalid_argument("Pulse-width scan does not constrain capture: include unsaturated and saturated filling");
    return {rate,rate/(n*v),amplitude,std::sqrt(error/t.size())};
}
void AnalyzeDlts(DltsData& data,const DltsConfig& c,bool laplace) {
    scaps_core::detail::throw_if_cancelled();
    data.spectra.clear();data.arrhenius.reset();data.capture.reset();
    const auto filters=DltsFilters(c);dlts::SpectraRequest scan;scan.filters=filters;
    std::vector<double> widths,amplitudes;
    for(auto& immutable:data.traces) {
        scaps_core::detail::throw_if_cancelled();
        if(immutable->status!="converged")continue;
        auto row=std::make_shared<DltsTrace>(*immutable);dlts::RateWindowRequest window;window.transient=dlts::Transient{row->time_s,row->capacitance_F,row->baseline_F};
        row->signal_F.clear();row->amplitude_F.clear();
        for(const auto& filter:filters){window.filter=filter;const auto filtered=dlts::rateWindow(window);row->signal_F.push_back(*filtered.signal_F);row->amplitude_F.push_back(*filtered.amplitude_F);}
        const double excursion=std::accumulate(row->capacitance_F.begin(),row->capacitance_F.end(),0.,[&](double best,double value){return std::max(best,std::abs(value-row->baseline_F));});
        const bool resolved=excursion>row->readout_resolution_F;
        if(!resolved)row->message="Transient is below the requested solver resolution; refine convergence before inferring trap parameters.";
        row->laplace.reset();
        if(laplace && resolved) {
            dlts::LaplaceRequest request;request.transient=std::move(*window.transient);request.grid_size=c.grid_size;request.rate_min_per_s=c.rate_min_per_s;request.rate_max_per_s=c.rate_max_per_s;request.polarity=c.polarity;request.regularization_order=c.regularization_order;request.alpha_method=c.alpha_method;request.alpha=c.alpha;
            row->laplace=dlts::laplace(request);
            *window.transient=std::move(request.transient);
        }
        scan.minimum_prominence_F=std::max(scan.minimum_prominence_F,row->readout_resolution_F);
        if(c.series==DltsSeries::Temperature || c.series==DltsSeries::DoublePulse)scan.traces.push_back({row->temperature_K,std::move(*window.transient)});
        if(resolved){widths.push_back(row->pulse_width_s);amplitudes.push_back(row->amplitude_F.front());}immutable=std::move(row);
    }
    if(scan.traces.size()>=3) {
        data.spectra=dlts::spectra(scan).windows;
        dlts::ArrheniusRequest request;request.dos_mass_m0=c.dos_mass_m0;request.velocity_mass_m0=c.velocity_mass_m0;request.degeneracy_factor=c.degeneracy_factor;
        // Multiple peaks are ambiguous; never silently combine different levels.
        for(const auto& window:data.spectra)if(window.peaks.size()==1){request.temperature_K.push_back(window.peaks[0].temperature_K);request.rate_per_s.push_back(window.peaks[0].rate_per_s);}
        if(request.temperature_K.size()>=3)try{data.arrhenius=dlts::arrhenius(request);}catch(const dlts::Error& e){data.warnings.push_back(e.what());}
        else data.warnings.push_back("Arrhenius needs three unambiguous rate-window peaks within the temperature scan.");
    }
    if(c.series==DltsSeries::PulseWidth && widths.size()>=4)try{data.capture=FitCapture(widths,amplitudes,c.capture_density_m3,c.capture_velocity_m_s);}catch(const std::invalid_argument& e){data.warnings.push_back(e.what());}
}
void SolveDlts(Operation& operation,JobContext& ctx,const scaps_core::Mesh1D& mesh) {
    using namespace scaps_core;
    const auto& a=*operation.action;const auto& c=a.dlts;const auto& p=operation.draft->project;ValidateDlts(c);
    const bool optical=a.kind==ActionKind::Optical,laplace=a.kind==ActionKind::Laplace;
    if(optical && !p.illumination.enabled)throw std::invalid_argument("MCTS / ODLTS requires enabled stage-6 illumination; configure the optical source first");
    auto device=make_device_from_project(p);if(!optical)device.illumination.enabled=false;
    const auto pc=physical_constants(p.constants_mode);const auto values=ActionSamples(a,p);
    auto result=CreateRef<AcResult>();result->kind=ActionId(a.kind);result->has=7u|(1u<<31);result->mesh_x_m=Column::Intern(mesh.x_m);result->area_m2=device.external.area_m2;
    auto data=std::make_shared<DltsData>();result->dlts=data;operation.typedOut=result;
    data->warnings={"N_T uses the dilute one-sided depletion approximation with the configured lambda correction; inspect recapture, barriers and Debye broadening.","Capture sigma assumes the specified local carrier density and thermal velocity; nonuniform injection can invalidate a single capture rate."};
    if(optical)data->warnings.push_back("Optical pulses inject carriers through prescribed generation. Direct optical trap ionization is outside the qualified engine.");
    const double eps=pc.eps0_Fm*c.profiling_eps_r,area=device.external.area_m2;
    try {for(std::size_t i=0;i<values.size();++i) {
        ctx.ThrowIfCancellationRequested();if(operation.cancellation.stop_requested()){data->cancelled=true;throw JobCancelledException();}
        auto row=std::make_shared<DltsTrace>();row->id="trace-"+std::to_string(i);
        row->temperature_K=(c.series==DltsSeries::Temperature || c.series==DltsSeries::DoublePulse)?values[i]:p.temperature_K;row->pulse_width_s=c.series==DltsSeries::PulseWidth?values[i]:c.pulse_width_s;row->reverse_V=c.series==DltsSeries::Depth?values[i]:c.reverse_V;row->pulse_V=optical && c.minority?row->reverse_V:c.pulse_V;
        ctx.SetStage(std::string(ActionId(a.kind))+" trace "+std::to_string(i+1)+" / "+std::to_string(values.size()));
        int pass=0;const int passes=c.series==DltsSeries::DoublePulse?2:1;
        const auto acquire=[&](double pulse)->std::pair<std::vector<double>,std::vector<double>> {
            TransientConfig time;time.retention=TransientRetention::ObservablesOnly;time.initial_step_s=c.initial_step_s;time.max_step_s=c.max_step_s;time.max_steps=c.max_steps;time.poole_frenkel=c.poole_frenkel;time.prebias_generation_multiplier=0;
            const double width=row->pulse_width_s,frequency=a.frequency_Hz;
            if(c.readout==DltsReadout::Frozen)time.readout_frequency_Hz=frequency;
            else {time.ac_amplitude_V=c.ac_amplitude_V;time.ac_frequency_Hz=frequency;time.max_step_s=std::min(time.max_step_s,1/(frequency*64));}
            std::vector<double> relative;
            for(int j=0;j<c.samples;++j){const double t=c.first_sample_s*std::pow(c.last_sample_s/c.first_sample_s,double(j)/(c.samples-1));relative.push_back(t);time.sample_times_s.push_back(width+t);if(c.readout==DltsReadout::Demodulated && t<1/frequency)throw std::invalid_argument("AC demodulation requires first sample >= one probe period");}
            time.progress=[&](double progress){ctx.SetProgress(float(i+(pass+progress)/passes),float(values.size()));if(operation.cancellation.stop_requested())throw ExecutionCancelled();};
            std::vector<TransientDrive> drive={{0,-pulse,optical?c.generation_multiplier:0},{width,-row->reverse_V,0}};
            double baseline=0;bool baselineSet=false;
            const auto solved=solve_transient(device,mesh,-row->reverse_V,row->temperature_K,drive,width+c.last_sample_s,time,p.solver,pc,{},[&](const TransientPoint& point,double origin){if(!baselineSet){const auto ac=solve_transient_ac(device,mesh,point,frequency,c.poole_frenkel,pc,origin);if(ac.status!=SolverStatus::Converged || !std::isfinite(ac.C_F))throw std::runtime_error("DLTS equilibrium C readout failed");baseline=ac.C_F;baselineSet=true;}return true;});
            if(solved.status==TransientStatus::Cancelled){data->cancelled=true;throw JobCancelledException();}
            if(solved.status!=TransientStatus::Converged)throw std::runtime_error(solved.message);
            std::vector<double> cap,current;
            if(c.readout==DltsReadout::Frozen) {
                if(solved.readouts.size()!=relative.size())throw std::runtime_error("DLTS readout count differs from requested sampling");
                for(const auto& readout:solved.readouts){if(readout.status!=SolverStatus::Converged || !std::isfinite(readout.C_F) || !std::isfinite(readout.current_conservation_relative) || readout.current_conservation_relative>.01)throw std::runtime_error("Invalid frozen DLTS readout");cap.push_back(readout.C_F);}
            } else {
                // One cumulative quadrature; sliding readouts then cost logarithmic time.
                const double omega=2*std::numbers::pi*frequency;
                std::vector<double> integral(solved.observables.size());
                for(std::size_t k=1;k<integral.size();++k){const auto& l=solved.observables[k-1];const auto& r=solved.observables[k];integral[k]=integral[k-1]+(r.time_s-l.time_s)*(l.current_density_A_m2*std::cos(omega*l.time_s)+r.current_density_A_m2*std::cos(omega*r.time_s))/2;}
                const auto at=[&](double stamp){const auto it=std::lower_bound(solved.observables.begin(),solved.observables.end(),stamp,[](const auto& p,double t){return p.time_s<t;});if(it==solved.observables.end())return integral.back();const auto k=std::size_t(it-solved.observables.begin());if(!k)return integral.front();const auto& l=solved.observables[k-1];const auto& r=*it;const double left=l.current_density_A_m2*std::cos(omega*l.time_s),right=r.current_density_A_m2*std::cos(omega*r.time_s),value=std::lerp(left,right,(stamp-l.time_s)/(r.time_s-l.time_s));return integral[k-1]+(stamp-l.time_s)*(left+value)/2;};
                for(double t:relative) {
                    const double end=width+t,start=end-1/frequency;
                    cap.push_back(-2*(at(end)-at(start))*frequency*area/(omega*c.ac_amplitude_V));
                }
            }
            for(double t:relative){const auto it=std::lower_bound(solved.observables.begin(),solved.observables.end(),width+t,[](const auto& point,double stamp){return point.time_s<stamp;});if(it==solved.observables.end())throw std::runtime_error("Missing terminal sample");current.push_back(it->current_density_A_m2);}
            row->time_s=Column::Intern(std::move(relative));row->baseline_F=baseline;row->equilibrium_F=baseline;++pass;return {std::move(cap),std::move(current)};
        };
        try {
            auto [cap,current]=acquire(row->pulse_V);row->current_A_m2=Column::Intern(std::move(current));
            if(c.series==DltsSeries::DoublePulse){const double baseline=row->baseline_F;auto paired=acquire(c.second_pulse_V);row->reference_capacitance_F=Column::Intern(std::move(paired.first));for(std::size_t j=0;j<cap.size();++j)cap[j]-=row->reference_capacitance_F[j];row->baseline_F=baseline-row->baseline_F;}
            row->capacitance_F=Column::Intern(std::move(cap));
            row->depth_m=row->equilibrium_F>0?eps*area/row->equilibrium_F:0;
            auto referenceDevice=device;referenceDevice.illumination.enabled=false;
            const auto pulse=solve_cv(referenceDevice,mesh,{-row->pulse_V},row->temperature_K,a.frequency_Hz,p.solver,pc).front();
            if(pulse.status==SolverStatus::Converged && pulse.C_F>0)row->pulse_depth_m=eps*area/pulse.C_F;
            const auto window=dlts::rateWindow({DltsFilters(c).front(),dlts::Transient{row->time_s,row->capacitance_F,row->baseline_F}});
            row->readout_resolution_F=std::abs(row->equilibrium_F)*std::max(p.solver.residual_tolerance,64*std::numeric_limits<double>::epsilon());
            if(!optical && std::abs(*window.amplitude_F)>row->readout_resolution_F && row->depth_m>row->pulse_depth_m && row->pulse_depth_m>0) {
                dlts::ConcentrationRequest request;request.donor_density_cm3=c.donor_density_m3/1e6;request.baseline_F=row->equilibrium_F;request.gain=window.gain;request.signal_F=*window.signal_F;request.relative_permittivity=c.profiling_eps_r;request.reverse_width_m=row->depth_m;request.pulse_width_m=row->pulse_depth_m;request.fermi_minus_trap_reverse_eV=c.fermi_minus_trap_reverse_eV;request.fermi_minus_trap_pulse_eV=c.fermi_minus_trap_pulse_eV;
                try {
                    const auto density=dlts::concentration(request);row->trap_density_m3=1e6*density.trap_density_cm3;row->density_valid=density.dilute;for(const auto& warning:density.warnings){if(!row->message.empty())row->message+="; ";row->message+=warning;}
                    if(c.series==DltsSeries::DoublePulse){const auto second=solve_cv(referenceDevice,mesh,{-c.second_pulse_V},row->temperature_K,a.frequency_Hz,p.solver,pc).front();if(second.status!=SolverStatus::Converged || second.C_F<=0)throw std::invalid_argument("Second-pulse depletion width unavailable");request.pulse_width_m=eps*area/second.C_F;const auto paired=dlts::concentration(request);const double shell=std::abs(density.f_lambda-paired.f_lambda);if(shell<1e-12)throw std::invalid_argument("DDLTS pulses do not resolve a depletion shell");row->trap_density_m3=2*c.donor_density_m3*std::abs(*window.amplitude_F)/row->equilibrium_F/shell;}
                }catch(const dlts::Error& e){row->density_valid=false;row->message=e.what();}catch(const std::invalid_argument& e){row->density_valid=false;row->message=e.what();}
            }

        } catch(const JobCancelledException&){data->cancelled=true;throw;}
        catch(const ExecutionCancelled&){data->cancelled=true;throw;}
        catch(const std::exception& e){row->status="solve_failed";row->message=e.what();}
        data->traces.push_back(row);if(operation.feed)operation.feed->Dlts(row);
        AcPoint point;point.has=((1u<<13)-1)|(1u<<15);point.point_id=row->id;point.voltage_V=row->reverse_V;point.frequency_Hz=a.frequency_Hz;point.temperature_K=row->temperature_K;point.capacitance_F=row->baseline_F;point.status=row->status;point.valid=row->status=="converged";point.validity_status=point.valid?"valid":"solve_failed";result->points.push_back(std::move(point));
    }
    }catch(const JobCancelledException&){data->cancelled=true;}catch(const ExecutionCancelled&){data->cancelled=true;}
    try{AnalyzeDlts(*data,c,laplace);}catch(const ExecutionCancelled&){data->cancelled=true;}catch(const dlts::Error& e){data->warnings.push_back(e.what());}
}
}
