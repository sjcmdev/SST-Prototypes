#pragma once
#include "AnalysisRunner.h"
#include "Core/JobSystem/JobContext.hpp"
#include <sst/physics/impedance.hpp>
#include <complex>
#include <span>
#include <functional>

namespace SST::Prototypes::CapLab {
// Owns the registry before the circuit, and all mutable state of one fit.
class PreparedISModel {
    struct Slot final : circuitcore::IParameterLaw {
        double value;
        explicit Slot(double v) : value(v) {}
        double evaluate(const circuitcore::ParameterEvaluationContext &) const override { return value; }
        const char *describe() const noexcept override { return "constant"; }
    };
    ccui::CircuitDocument document_;
    circuitcore::DeviceRegistry registry_ = circuitcore::DeviceRegistry::createWithBuiltinDevices();
    ccui::BuiltDocument built_;
    circuitcore::DiagnosticSink sink_;
    circuitcore::CompiledCircuit compiled_;
    circuitcore::ParameterSet parameters_;
    circuitcore::SimulationEnvironment environment_;
    circuitcore::AcSweepOptions options_;
    std::vector<std::shared_ptr<Slot>> slots_;
    std::vector<std::pair<std::size_t,std::string>> fields_;
    std::vector<double> frequencies_;
    std::vector<std::complex<double>> measured_, residuals_;
public:
    struct Diagnostic { circuitcore::SimulationDiagnostic value; std::optional<std::size_t> row; };
private:
    std::vector<std::complex<double>> model_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t evaluations_ = 0;
public:
    using Compiler=std::function<circuitcore::CompiledCircuit(const circuitcore::CircuitDefinition &,const circuitcore::DeviceRegistry &,circuitcore::DiagnosticSink &)>;
    PreparedISModel(ccui::CircuitDocument document, std::vector<double> frequencies,
        std::vector<std::complex<double>> measured, double temperature, double bias, const Compiler &compile = circuitcore::Simulator::compile)
        : document_(std::move(document)), built_(ccui::compileDocument(document_,true)),
          compiled_(compile(built_.definition,registry_,sink_)),
          parameters_(compiled_.defaultParameters()), frequencies_(std::move(frequencies)),
          measured_(std::move(measured)), residuals_(frequencies_.size()), model_(frequencies_.size()) {
        if (frequencies_.empty() || measured_.size()!=frequencies_.size()) throw std::runtime_error("Unequal/empty prepared IS arrays");
        parameters_.set(built_.portSource,circuitcore::SourceParameters::DcValue,bias);
        parameters_.set(built_.portSource,circuitcore::SourceParameters::AcMagnitude,1.);
        environment_.temperatureK=temperature;
        options_.observations.observeDeviceCurrent(built_.portSource);
        options_.frequenciesHz.resize(1);
    }
    PreparedISModel(const PreparedISModel &) = delete;
    PreparedISModel &operator=(const PreparedISModel &) = delete;
    std::size_t bind(std::size_t element, const std::string &field) {
        const auto &e=document_.elements.at(element);
        const bool diode=e.kind==ccui::Kind::PN || e.kind==ccui::Kind::Schottky;
        const std::vector<std::string> names=diode ? std::vector<std::string>{"value","secondary","Cj0","Vbi","m","FC","tau"} :
            e.kind==ccui::Kind::CPE ? std::vector<std::string>{"value","secondary"} : std::vector<std::string>{"value"};
        const auto it=std::find(names.begin(),names.end(),field);
        if (it==names.end()) throw std::runtime_error("Unsupported prepared IS parameter");
        const circuitcore::ParameterId parameter{static_cast<std::size_t>(it-names.begin())};
        const auto device=built_.devices.at(element);
        auto slot=std::make_shared<Slot>(parameters_.evaluate(device,parameter,{}));
        parameters_.set(device,parameter,slot);
        slots_.push_back(std::move(slot)); fields_.emplace_back(element,field);
        return slots_.size()-1;
    }
    void set(std::size_t slot, double value) {
        if (!std::isfinite(value)) throw std::runtime_error("Invalid element or duplicate name");
        const auto &field=fields_.at(slot).second;
        if ((field=="Cj0" || field=="tau") && value<0 || field=="Vbi" && value<=0 ||
            field=="m" && (value<0 || value>1) || field=="FC" && (value<0 || value>=1))
            throw std::runtime_error("Invalid diode capacitance parameters");
        slots_.at(slot)->value=value;
        const auto index=fields_.at(slot).first;
        auto &e=document_.elements.at(index);
        if (field=="value") e.value=value;
        else if (field=="secondary") e.secondary=value;
        else if (field=="Cj0") e.cj0=value;
        else if (field=="Vbi") e.vbi=value;
        else if (field=="m") e.m=value;
        else if (field=="FC") e.fc=value;
        else if (field=="tau") e.tau=value;
    }
    bool evaluate(JobContext &context, bool details = false) {
        ++evaluations_;
        if (details) {
            diagnostics_.clear();
            for (const auto &d : sink_.entries()) diagnostics_.push_back({d,{}});
        }
        if (sink_.hasErrors()) return false;
        // Each old simulation started from the same fresh state, including diode charge.
        const auto state=compiled_.createInitialState(parameters_);
        for (std::size_t i=0;i<frequencies_.size();++i) {
            context.ThrowIfCancellationRequested();
            options_.frequenciesHz[0]=frequencies_[i];
            const auto result=circuitcore::Simulator::acSweep(compiled_,parameters_,environment_,state,options_);
            if (details) for (const auto &d : result.diagnostics) diagnostics_.push_back({d,i});
            if (result.points.empty() || !circuitcore::isUsableResult(result.points[0].status) || result.points[0].deviceCurrents.empty()) return false;
            const auto model=sst::physics::reciprocalImmittance(-result.points[0].deviceCurrents[0]);
            if (details) model_[i]=model;
            residuals_[i]={model.real()-measured_[i].real(),model.imag()-measured_[i].imag()};
        }
        return true;
    }
    std::span<const std::complex<double>> residuals() const { return residuals_; }
    const ccui::CircuitDocument &document() const { return document_; }
    std::span<const std::complex<double>> measured() const { return measured_; }
    std::span<const std::complex<double>> model() const { return model_; }
    const auto &diagnostics() const { return diagnostics_; }
    std::size_t evaluations() const { return evaluations_; }
};
}
