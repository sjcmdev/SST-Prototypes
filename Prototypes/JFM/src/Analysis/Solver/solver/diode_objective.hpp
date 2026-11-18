// src/solver/diode_objective.hpp
#pragma once
#include <sst/math/solver_types.hpp>
#include <sst/math/nelder_mead.hpp> // dla sst::math::SANelderMead::ObjectiveFn

#include <functional>
#include <vector>

/// Buduje funkcję celu (sst::math::SANelderMead::ObjectiveFn) wiążącą model IV z
/// konkretnym zestawem danych pomiarowych. Wynikowa funkcja przyjmuje
/// WYŁĄCZNIE wolne parametry i zwraca chi2.
///
/// all_params — pełna lista parametrów (free+fixed) w kolejności modelu;
///              MUSI być tym samym wektorem (ta sama kolejność) co przekazany
///              do konstruktora sst::math::SANelderMead, inaczej mapowanie się rozjedzie.
/// model_fn   — np. [](double V, const double* p){ return evaluateDiodeIV4(V, p); }
/// V_data, I_meas, sigma — dane pomiarowe; muszą mieć tę samą długość
///
/// Rzuca std::invalid_argument gdy długości V_data/I_meas/sigma się różnią.
sst::math::SANelderMead::ObjectiveFn makeDiodeIVObjective(
    std::vector<sst::math::FitParam> all_params,
    std::function<double(double, const double *)> model_fn,
    std::vector<double> V_data,
    std::vector<double> I_meas,
    std::vector<double> sigma);