#include "Core/dspch.hpp"
#include "IO/JFMMonteCarloStorage.hpp"
#include <sst/hdf5/storage.hpp>
#include <cmath>
#include <sstream>
#include <iomanip>
#include "Core/IO/TextFileIO.hpp"
#include "Analysis/JFMFitEngine.hpp"
#include "Core/JobSystem/JobSystemTypes.hpp"
#include <limits>

namespace SST::Prototypes::JFM {
namespace {
StructuredError StorageError(const std::exception& e) {
    return {ErrorCategory::IO, Severity::Error, "Monte Carlo samples could not be saved or read.",
        e.what(), "Keep the result open and retry, or save the project to a writable directory.", "JFM", "jfm.mc.storage"};
}
constexpr std::size_t SampleFields = 15;
int Integer(double value) {
    if (!std::isfinite(value) || value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max() || value != std::trunc(value))
        throw std::runtime_error("Invalid integer in MC sample dataset.");
    return static_cast<int>(value);
}
}

Result<void> SaveJFMMonteCarloHdf5(const Path& file, const JFMMonteCarloResult& result, const std::function<void()>& checkpoint) {
    try {
        const auto points = result.voltage.size(), parameters = result.parameterNames.size();
        if (result.samples.empty() || !parameters || parameters > 8 || !points || result.idealCurrent.size() != points
            || (!result.currentSigma.empty() && result.currentSigma.size() != points)
            || (result.noiseSeed && result.noiseAmplitude.size() != points)
            || result.noisyCurrent.size() % points != 0 || result.noisyCurrent.size()/points > result.samples.size())
            throw std::runtime_error("Inconsistent MC sample dimensions.");
        sst::hdf5::Attributes attributes{{"schema", "jfm.mc.1"}, {"parameter_count", std::to_string(parameters)},
            {"free_parameter_count", std::to_string(result.freeParameterCount)}, {"confidence_available", result.confidenceAvailable ? "1" : "0"},
            {"noise_seed", result.noiseSeed ? std::to_string(*result.noiseSeed) : ""},
            {"has_sigma", result.currentSigma.empty() ? "0" : "1"}, {"has_noise", result.noisyCurrent.empty() ? "0" : "1"}};
        for (std::size_t i=0; i<parameters; ++i) attributes["parameter_"+std::to_string(i)] = result.parameterNames[i];
        std::vector<double> values;
        values.reserve(result.samples.size()*(parameters+SampleFields));
        std::map<std::string, int> reasons, methods;
        auto dictionary = [&](const std::string& value, std::map<std::string,int>& entries, const char* prefix) {
            const auto [entry, added] = entries.try_emplace(value, static_cast<int>(entries.size()));
            if (added) attributes[std::string(prefix)+std::to_string(entry->second)] = value;
            return static_cast<double>(entry->second);
        };
        for (std::size_t id=0; id<result.samples.size(); ++id) {
            if (checkpoint && id%256 == 0) checkpoint();
            const auto& s=result.samples[id];
            if (s.parameters.size()!=parameters) throw std::runtime_error("Inconsistent MC parameter count.");
            values.insert(values.end(),s.parameters.begin(),s.parameters.end());
            values.insert(values.end(), {static_cast<double>(id), s.chiSquared, s.reducedChiSquared, static_cast<double>(s.iterations),
                double(s.converged), double(s.accepted), double(s.solverConverged), dictionary(s.stopReason,reasons,"reason_"),
                dictionary(s.fitMethod,methods,"method_"), double(s.flatDirections), double(s.lmStatus), s.objectiveChiSquared,
                double(s.linearPoints), double(s.logPoints), double(s.usedGpu)});
        }
        std::vector<sst::hdf5::Dataset> datasets;
        datasets.push_back({"/samples", {result.samples.size(),parameters+SampleFields}, values, "per parameter; fit input units", {}, {}, 0});
        auto add = [&](const char* id, const std::vector<double>& data, const char* units) {
            if (!data.empty()) datasets.push_back({id, {data.size()}, data, units, {}, {}, 0});
        };
        add("/voltage", result.voltage, "V"); add("/ideal_current",result.idealCurrent,"fit input current units");
        add("/sigma", result.currentSigma,"fit input current units");
        if (result.noiseSeed) add("/noise_amplitude",result.noiseAmplitude,"fit input current units");
        // The seeded recipe reproduces every noisy curve; retain legacy unseeded data verbatim.
        if (!result.noiseSeed) add("/noisy_current",result.noisyCurrent,"fit input current units");
        attributes["has_noise"] = !result.noiseSeed && !result.noisyCurrent.empty() ? "1" : "0";
        std::filesystem::create_directories(file.Native().parent_path());
        sst::hdf5::save(file.Native(),datasets,attributes,checkpoint);
        return {};
    } catch (const JobCancelledException&) { throw; }
      catch (const sst::hdf5::Error& e) { return StorageError(e); }
      catch (const std::runtime_error& e) { return StorageError(e); }
}

Result<JFMMonteCarloResult> ReadJFMMonteCarloHdf5(const Path& file) {
    try {
        auto table=sst::hdf5::read(file.Native(),"/samples");
        const auto& a=table.fileAttributes;
        if (a.at("schema")!="jfm.mc.1") throw std::runtime_error("Unsupported MC sample schema.");
        const auto parameters=std::stoull(a.at("parameter_count"));
        if (!parameters || parameters>8 || table.shape.size()!=2 || table.shape[1]!=parameters+SampleFields
            || table.shape[0]>64*1024*1024/sizeof(JFMMonteCarloSample)) throw std::runtime_error("Invalid MC table dimensions.");
        JFMMonteCarloResult result;
        result.freeParameterCount=std::stoi(a.at("free_parameter_count"));
        if (result.freeParameterCount<0 || result.freeParameterCount>parameters) throw std::runtime_error("Invalid MC free parameter count.");
        result.confidenceAvailable=a.at("confidence_available")=="1";
        for (std::size_t i=0;i<parameters;++i) result.parameterNames.push_back(a.at("parameter_"+std::to_string(i)));
        auto read=[&](const char* id) { return sst::hdf5::read(file.Native(),id).values; };
        result.voltage=read("/voltage"); result.idealCurrent=read("/ideal_current");
        if (a.at("has_sigma")=="1") result.currentSigma=read("/sigma");
        if (!a.at("noise_seed").empty()) { result.noiseSeed=std::stoull(a.at("noise_seed")); result.noiseAmplitude=read("/noise_amplitude"); }
        if (a.at("has_noise")=="1") result.noisyCurrent=read("/noisy_current");
        const auto points=result.voltage.size();
        if (!points || result.idealCurrent.size()!=points || (!result.currentSigma.empty() && result.currentSigma.size()!=points)
            || (result.noiseSeed && result.noiseAmplitude.size()!=points) || result.noisyCurrent.size()%points!=0
            || result.noisyCurrent.size()/points>table.shape[0]) throw std::runtime_error("Invalid MC curve dimensions.");
        result.samples.reserve(table.shape[0]);
        for (std::size_t id=0;id<table.shape[0];++id) {
            const auto begin=table.values.begin()+id*(parameters+SampleFields);
            const double* v=&*begin+parameters;
            if (Integer(v[0])!=id) throw std::runtime_error("Invalid MC sample ID.");
            JFMMonteCarloSample s;
            s.parameters.assign(begin,begin+parameters); s.chiSquared=v[1]; s.reducedChiSquared=v[2]; s.iterations=Integer(v[3]);
            s.converged=Integer(v[4])!=0; s.accepted=Integer(v[5])!=0; s.solverConverged=Integer(v[6])!=0;
            s.stopReason=a.at("reason_"+std::to_string(Integer(v[7]))); s.fitMethod=a.at("method_"+std::to_string(Integer(v[8])));
            s.flatDirections=Integer(v[9])!=0; s.lmStatus=Integer(v[10]); s.objectiveChiSquared=v[11];
            s.linearPoints=Integer(v[12]); s.logPoints=Integer(v[13]); s.usedGpu=Integer(v[14])!=0;
            result.samples.push_back(std::move(s));
        }
        return result;
    } catch (const std::exception& e) { return StorageError(e); }
}

		Result<void> ExportJFMMonteCarloTsv(const Path& directory, const JFMMonteCarloResult &result, double noisePercent, const std::function<void()>& checkpoint)
		{
            const auto parameterPath=directory/Path("parameters.tsv"), noisePath=directory/Path("noise.tsv");
            std::string error;
            auto flush=[](FILE *stream,std::ostringstream &buffer) {
                const auto text=buffer.str(); buffer.str({});
                return std::fwrite(text.data(),1,text.size(),stream)==text.size();
            };
            if (!TextFileIO::Save(parameterPath,[&](FILE *stream) {
			std::ostringstream parameters;
			parameters << std::setprecision(17) << "sample";
			for (const std::string &name : result.parameterNames)
				parameters << '\t' << name;
			parameters << "\tchi_squared\treduced_chi_squared\titerations\tconverged\taccepted\tsolver_converged\tstop_reason\tconfidence_available\tfit_method\tflat_directions\tlm_status\tobjective_chi_squared\tlinear_points\tlog_points\n";
			for (std::size_t index = 0; index < result.samples.size(); ++index)
			{
				if (checkpoint) checkpoint();
                const JFMMonteCarloSample &sample = result.samples[index];
				parameters << index;
				for (double value : sample.parameters)
					parameters << '\t' << value;
				parameters << '\t' << sample.chiSquared << '\t' << sample.reducedChiSquared << '\t' << sample.iterations << '\t' << (sample.solverConverged ? 1 : 0) << '\t' << (sample.accepted ? 1 : 0) << '\t' << (sample.solverConverged ? 1 : 0) << '\t' << sample.stopReason << '\t' << (result.confidenceAvailable ? 1 : 0) << '\t' << sample.fitMethod << '\t' << (sample.flatDirections ? 1 : 0) << '\t' << sample.lmStatus << '\t' << sample.objectiveChiSquared << '\t' << sample.linearPoints << '\t' << sample.logPoints << '\n';
                if(parameters.tellp()>=64*1024 && !flush(stream,parameters))return false;
			}
            return flush(stream,parameters);
            },error))
				return StorageError(std::runtime_error("Monte Carlo parameters TSV could not be saved: "+error));

            if (!TextFileIO::Save(noisePath,[&](FILE *stream) {
			std::ostringstream noise;
			noise << std::setprecision(17) << "sample\tvoltage\tideal_current\tnoisy_current\tnoise\tsigma\n";
			const std::size_t points = result.voltage.size();
			const double factor = std::max(0.0, noisePercent) / 100.0;
			for (std::size_t sample = 0; sample < result.samples.size(); ++sample)
			{
                if (checkpoint) checkpoint();
                const auto curve=JFMMonteCarloNoisyCurve(result,sample);
				for (std::size_t point = 0; point < points; ++point)
				{
					if (point >= curve.size())
						break;
					const double ideal = point < result.idealCurrent.size() ? result.idealCurrent[point] : 0.0;
					const double sigma = point < result.currentSigma.size() ? result.currentSigma[point] : std::abs(ideal * factor);
					const double noisy = curve[point];
					noise << sample << '\t' << result.voltage[point] << '\t' << ideal << '\t' << noisy << '\t' << noisy - ideal << '\t' << sigma << '\n';
                    if(noise.tellp()>=64*1024 && !flush(stream,noise))return false;
				}
			}
            return flush(stream,noise);
            },error))
				return StorageError(std::runtime_error("Monte Carlo noise TSV could not be saved: "+error));
			return {};
		}

}
