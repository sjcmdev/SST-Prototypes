```cpp
void FourParameterModel::current(Data &data, const NumericStorm::Fitting::Parameters<4> &parameters, const JFMAdditionalParameters &additionalParameters)
    {
        // I(V) = I0(e^(q(V-IRs)/AKT)-1) - (V - IRs)/Rsh
        NumericStorm::Fitting::Parameters<4> params(parameters);
        JFMAdditionalParameters additionalParams(additionalParameters);
        auto adjusted = adjustFixingConfiguration<4>(params, additionalParams);
        auto [I0, A, Rs, Rsh] = adjusted;
        const double k = 8.6e-5;

        auto func = [&](double &V, double &I, double &I0, double &A, double &Rsh, double &Rs, double T)
        {
            double x = ((I0 * Rs) / (A * k * T)) * std::exp(V / (A * k * T));
            double I_lw = utl::LambertW<0>(x);
            I_lw *= (A * k * T) / Rs;
            I = I_lw + (V - I_lw * Rs) / Rsh;
        };

        for (const auto &[V, I] : std::views::zip(data[0], data[1]))
            func(V, I, I0, A, Rsh, Rs, additionalParameters.Temperature);
    }
```
```cpp
void SixParameterModel::current(Data &data, const NumericStorm::Fitting::Parameters<6> &parameters, const JFMAdditionalParameters &additionalParameters)
    {
        // I(V) = I0(e^(q(V-IRs)/AKT)-1) - (V - IRs)/Rsh - (V - IRs)^alpha/Rsh2
        NumericStorm::Fitting::Parameters<6> params(parameters);
        JFMAdditionalParameters additionalParams(additionalParameters);
        auto adjusted = adjustFixingConfiguration<6>(params, additionalParams);
        auto [I0, A, Rs, Rsh, alpha, Rsh2] = adjusted; // utils::cast<6>(adjusted);
        const double k = 8.6e-5;

        auto func = [&](double &V, double &I, double &I0, double &A, double &Rsh, double &Rs, double &alpha, double &Rsh2, double T)
        {
            double x = ((I0 * Rs) / (A * k * T)) * std::exp(V / (A * k * T));
            double I_lw = utl::LambertW<0>(x);
            I_lw *= (A * k * T) / Rs;
            double additionalFactor = std::pow((V - I_lw * Rs), alpha) / Rsh2;
            I = I_lw + (V - I_lw * Rs) / Rsh + additionalFactor;
        };

        for (const auto &[V, I] : std::views::zip(data[0], data[1]))
            func(V, I, I0, A, Rsh, Rs, alpha, Rsh2, additionalParameters.Temperature);
    }
```


