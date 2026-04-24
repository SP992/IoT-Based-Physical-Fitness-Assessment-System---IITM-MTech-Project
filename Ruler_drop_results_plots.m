%% =========================================================
%  RULER DROP TEST — Statistical Analysis
%  IoT-Based Physical Fitness Assessment System
%  IIT Madras — MTech Biomedical Engineering
%  =========================================================
%  HOW TO USE:
%  1. Place your CSV file in the same folder as this script
%  2. Change the filename below if needed
%  3. Run the script — all figures and a summary will be generated
% ==========================================================

clc; clear; close all;

%% ── 1. LOAD DATA ──────────────────────────────────────────
data = readtable("C:\projects\.venv\HILPCS\TOF\RulerDropResults.csv");   % <-- change filename if needed

manual   = data.Manual;               % ground truth (cm)
acquired = data.Acquired;             % IoT system reading (cm)
n        = length(manual);

fprintf('==============================================\n');
fprintf('   RULER DROP TEST — STATISTICAL ANALYSIS\n');
fprintf('==============================================\n');
fprintf('Total trials analysed: %d\n\n', n);

%% ── 2. COMPUTE ERROR METRICS ──────────────────────────────

% --- Absolute Error per trial ---
% What it is: how far off was each individual reading, ignoring sign
% Why useful: tells you the magnitude of error for each trial
abs_error = abs(manual - acquired);

% --- Signed Error per trial ---
% What it is: acquired - manual (positive = overestimate, negative = underestimate)
% Why useful: tells you if your system has a consistent BIAS in one direction
signed_error = acquired - manual;

% --- Mean Absolute Error (MAE) ---
% What it is: average of all absolute errors
% Why useful: single number summarising "on average, how wrong is my system"
% Good MAE = as close to 0 as possible
MAE = mean(abs_error);

% --- Root Mean Square Error (RMSE) ---
% What it is: square root of average of squared errors
% Why useful: penalises large errors more than small ones
%             if RMSE >> MAE, you have occasional big blunders
%             if RMSE ≈ MAE, your errors are consistent/small
RMSE = sqrt(mean((manual - acquired).^2));

% --- Mean Absolute Percentage Error (MAPE) ---
% What it is: average error as a percentage of the true value
% Why useful: scale-independent — tells you error relative to the measurement
%             e.g. 2cm error on a 5cm drop is 40% (bad)
%                  2cm error on a 20cm drop is 10% (acceptable)
% Note: we exclude trials where manual = 0 to avoid division by zero
nonzero = manual ~= 0;
MAPE = mean(abs_error(nonzero) ./ manual(nonzero)) * 100;

% --- Pearson Correlation Coefficient (r) ---
% What it is: measures how linearly related manual and IoT readings are
% Why useful: r = 1.0 means perfect agreement in trend
%             r > 0.95 is considered excellent for medical/sports devices
%             NOTE: high r does NOT mean no error — it means they move together
r = corr(manual, acquired);
r_squared = r^2;

% --- Coefficient of Variation of RMSE (CVRMSE) ---
% What it is: RMSE expressed as % of the mean manual value
% Why useful: contextualises error relative to the range of measurements
CVRMSE = (RMSE / mean(manual)) * 100;

% --- Bias (Mean Signed Error) ---
% What it is: average of signed errors
% Why useful: if bias > 0, your system consistently OVERestimates
%             if bias < 0, your system consistently UNDERestimates
%             if bias ≈ 0, errors are random (good)
bias = mean(signed_error);

% --- Limits of Agreement (Bland-Altman) ---
% What it is: range within which 95% of differences between methods fall
% Why useful: standard method in biomedical engineering to compare
%             a new measurement device against a reference/gold standard
%             LoA = bias ± 1.96 × SD of differences
SD_diff  = std(signed_error);
LoA_upper = bias + 1.96 * SD_diff;
LoA_lower = bias - 1.96 * SD_diff;

%% ── 3. PRINT SUMMARY ──────────────────────────────────────
fprintf('──────────────────────────────────────────────\n');
fprintf('METRIC                        VALUE\n');
fprintf('──────────────────────────────────────────────\n');
fprintf('Mean Absolute Error (MAE)   : %.3f cm\n',  MAE);
fprintf('Root Mean Square Error      : %.3f cm\n',  RMSE);
fprintf('Mean Abs %% Error (MAPE)     : %.2f %%\n', MAPE);
fprintf('Pearson Correlation (r)     : %.4f\n',     r);
fprintf('R-squared (R²)              : %.4f\n',     r_squared);
fprintf('Bias (mean signed error)    : %.3f cm\n',  bias);
fprintf('SD of differences           : %.3f cm\n',  SD_diff);
fprintf('Bland-Altman LoA Upper      : %.3f cm\n',  LoA_upper);
fprintf('Bland-Altman LoA Lower      : %.3f cm\n',  LoA_lower);
fprintf('CV-RMSE                     : %.2f %%\n',  CVRMSE);
fprintf('──────────────────────────────────────────────\n\n');

%% ── 4. FIGURES ────────────────────────────────────────────
% Clean style
set(0,'DefaultAxesFontName','Arial','DefaultAxesFontSize',12);
navy  = [0  0.231 0.435];
red   = [0.784 0.063 0.180];
lgray = [0.9 0.9 0.9];

% ── FIGURE 1: Scatter Plot — Manual vs Acquired ────────────
figure('Name','Scatter: Manual vs Acquired','Position',[100 100 600 520]);
hold on;
% Identity line (perfect agreement)
lim = [0, max([manual; acquired])*1.15];
plot(lim, lim, '--', 'Color',[0.5 0.5 0.5], 'LineWidth',1.5, ...
     'DisplayName','Perfect Agreement (y = x)');
% Linear fit
p = polyfit(manual, acquired, 1);
x_fit = linspace(lim(1), lim(2), 100);
plot(x_fit, polyval(p,x_fit), '-', 'Color',red, 'LineWidth',1.8, ...
     'DisplayName',sprintf('Linear Fit (R²=%.3f)', r_squared));
% Data points
scatter(manual, acquired, 60, navy, 'filled', 'MarkerFaceAlpha',0.8, ...
        'DisplayName','Trials');
hold off;
xlabel('Manual Measurement (cm)', 'FontWeight','bold');
ylabel('IoT Acquired Value (cm)',  'FontWeight','bold');
title('Ruler Drop Test: Manual vs IoT Measurement', 'FontWeight','bold');
legend('Location','northwest');
xlim(lim); ylim(lim);
grid on; box on;
text(lim(2)*0.05, lim(2)*0.88, ...
     sprintf('r = %.4f\nMAE = %.2f cm\nMAPE = %.1f%%', r, MAE, MAPE), ...
     'FontSize',11, 'BackgroundColor','w', 'EdgeColor',[0.7 0.7 0.7]);
saveas(gcf, 'fig1_scatter_manual_vs_acquired.png');

% ── FIGURE 2: Bland-Altman Plot ────────────────────────────
% This is the gold standard for comparing two measurement methods
% X-axis: mean of both methods (best estimate of true value)
% Y-axis: difference between methods (acquired - manual)
% If points are randomly scattered around 0 within LoA — your device is good
figure('Name','Bland-Altman Plot','Position',[100 100 600 520]);
means_BA = (manual + acquired) / 2;
hold on;
yline(bias,     '-',  'Color',navy, 'LineWidth',2,   'DisplayName',sprintf('Bias = %.2f cm', bias));
yline(LoA_upper,'--', 'Color',red,  'LineWidth',1.5, 'DisplayName',sprintf('+1.96SD = %.2f cm', LoA_upper));
yline(LoA_lower,'--', 'Color',red,  'LineWidth',1.5, 'DisplayName',sprintf('-1.96SD = %.2f cm', LoA_lower));
yline(0,        ':',  'Color',[0.5 0.5 0.5], 'LineWidth',1, 'DisplayName','Zero difference');
scatter(means_BA, signed_error, 60, navy, 'filled', 'MarkerFaceAlpha',0.8, 'DisplayName','Trials');
hold off;
xlabel('Mean of Manual and IoT (cm)', 'FontWeight','bold');
ylabel('IoT − Manual (cm)',           'FontWeight','bold');
title('Bland-Altman Plot: Agreement Between Methods', 'FontWeight','bold');
legend('Location','best');
grid on; box on;
saveas(gcf, 'fig2_bland_altman.png');

% ── FIGURE 3: Absolute Error per Trial ────────────────────
figure('Name','Error per Trial','Position',[100 100 700 420]);
bar(1:n, abs_error, 'FaceColor',navy, 'EdgeColor','none', 'FaceAlpha',0.85);
hold on;
yline(MAE, '--r', 'LineWidth',2, 'DisplayName',sprintf('MAE = %.2f cm', MAE));
hold off;
xlabel('Trial Number', 'FontWeight','bold');
ylabel('Absolute Error (cm)', 'FontWeight','bold');
title('Absolute Error per Trial', 'FontWeight','bold');
legend; grid on; box on;
xlim([0 n+1]);
saveas(gcf, 'fig3_error_per_trial.png');

% ── FIGURE 4: Error Distribution Histogram ────────────────
figure('Name','Error Distribution','Position',[100 100 560 460]);
histogram(signed_error, 8, 'FaceColor',navy, 'EdgeColor','w', 'FaceAlpha',0.85);
hold on;
xline(bias, '--r', 'LineWidth',2, 'DisplayName',sprintf('Bias = %.2f cm', bias));
xline(0,    ':k',  'LineWidth',1.5,'DisplayName','Zero error');
hold off;
xlabel('Signed Error: IoT − Manual (cm)', 'FontWeight','bold');
ylabel('Frequency', 'FontWeight','bold');
title('Distribution of Measurement Errors', 'FontWeight','bold');
legend; grid on; box on;
saveas(gcf, 'fig4_error_distribution.png');

% ── FIGURE 5: Side-by-side comparison bar chart ───────────
figure('Name','Trial Comparison','Position',[100 100 800 420]);
x = 1:n;
bar(x - 0.2, manual,   0.35, 'FaceColor',navy, 'EdgeColor','none', 'FaceAlpha',0.9, 'DisplayName','Manual');
hold on;
bar(x + 0.2, acquired, 0.35, 'FaceColor',red,  'EdgeColor','none', 'FaceAlpha',0.9, 'DisplayName','IoT Acquired');
hold off;
xlabel('Trial Number', 'FontWeight','bold');
ylabel('Drop Distance (cm)', 'FontWeight','bold');
title('Manual vs IoT Measurement — All Trials', 'FontWeight','bold');
legend; grid on; box on; xlim([0 n+1]);
saveas(gcf, 'fig5_trial_comparison.png');

fprintf('✓ All figures saved as PNG in the current folder.\n');
fprintf('✓ Use fig2_bland_altman.png in your report — it is the\n');
fprintf('  standard biomedical method comparison plot.\n');
fprintf('  Examiners at IITM will recognise and appreciate it.\n\n');