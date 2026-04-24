%% =========================================================
%  SIT AND REACH TEST — Statistical Analysis
%  IoT-Based Physical Fitness Assessment System
%  IIT Madras — MTech Biomedical Engineering
%  =========================================================
%  HOW TO USE:
%  1. Place your CSV file in the same folder as this script
%  2. Change the filename below if needed
%  3. Run — all figures saved as PNG
% ==========================================================

clc; clear; close all;

%% ── CONFIG ───────────────────────────────────────────────
% Inline data — change or replace with readtable() if needed
manual   = [17.5; 28; 36; 42; 8];
acquired = [16.7; 26.7; 35.7; 41.2; 8.3];

% If you want to load from CSV instead, uncomment:
% data     = readtable('sit_and_reach.csv');
% manual   = data.Manual;
% acquired = data.Acquired;

n = length(manual);

fprintf('==============================================\n');
fprintf('   SIT AND REACH TEST — STATISTICAL ANALYSIS\n');
fprintf('==============================================\n');
fprintf('Total trials analysed: %d\n\n', n);

% Colors
navy   = [0.000 0.231 0.435];
red    = [0.784 0.063 0.180];
green  = [0.180 0.588 0.180];
orange = [0.900 0.450 0.100];
dgray  = [0.400 0.400 0.400];
lgray  = [0.900 0.900 0.900];

set(0,'DefaultAxesFontName','Arial','DefaultAxesFontSize',12);

%% ── METRICS ──────────────────────────────────────────────

abs_error    = abs(manual - acquired);
signed_error = acquired - manual;

% MAE
MAE = mean(abs_error);

% RMSE
RMSE = sqrt(mean((manual - acquired).^2));

% MAPE
nonzero = manual ~= 0;
MAPE = mean(abs_error(nonzero) ./ manual(nonzero)) * 100;

% Pearson r and R²
r         = corr(manual, acquired);
r_squared = r^2;

% Bias and SD
bias    = mean(signed_error);
SD_diff = std(signed_error);

% Bland-Altman LoA
LoA_upper = bias + 1.96 * SD_diff;
LoA_lower = bias - 1.96 * SD_diff;

% CVRMSE
CVRMSE = (RMSE / mean(manual)) * 100;

%% ── PRINT SUMMARY ────────────────────────────────────────
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

%% ── FIGURE 1: Scatter — Manual vs Acquired ───────────────
figure('Name','Scatter: Manual vs Acquired','Position',[100 100 580 520]);
hold on;

lim = [0, max([manual; acquired]) * 1.15];

plot(lim, lim, '--', 'Color', dgray, 'LineWidth', 1.5, ...
     'DisplayName', 'Perfect agreement (y = x)');

p_fit = polyfit(manual, acquired, 1);
x_fit = linspace(lim(1), lim(2), 100);
plot(x_fit, polyval(p_fit, x_fit), '-', 'Color', red, 'LineWidth', 1.8, ...
     'DisplayName', sprintf('Linear fit (R²=%.3f)', r_squared));

scatter(manual, acquired, 70, navy, 'filled', 'MarkerFaceAlpha', 0.85, ...
        'DisplayName', 'Trials');

% Label each trial
for i = 1:n
    text(manual(i)+0.5, acquired(i)+0.5, sprintf('T%d', i), ...
         'FontSize', 10, 'Color', dgray);
end

hold off;
xlabel('Manual Measurement (cm)', 'FontWeight', 'bold');
ylabel('IoT Acquired Value (cm)',  'FontWeight', 'bold');
title('Sit and Reach Test: Manual vs IoT Measurement', 'FontWeight', 'bold');
legend('Location', 'northwest');
xlim(lim); ylim(lim);
grid on; box on;
text(lim(2)*0.05, lim(2)*0.88, ...
     sprintf('r = %.4f\nMAE = %.2f cm\nMAPE = %.1f%%', r, MAE, MAPE), ...
     'FontSize', 10, 'BackgroundColor', 'w', 'EdgeColor', [0.7 0.7 0.7]);
saveas(gcf, 'sr_fig1_scatter.png');

%% ── FIGURE 2: Bland-Altman ───────────────────────────────
figure('Name','Bland-Altman','Position',[100 100 580 520]);
means_BA = (manual + acquired) / 2;
hold on;

yline(bias,     '-',  'Color', navy, 'LineWidth', 2.0, ...
      'DisplayName', sprintf('Bias = %.2f cm', bias));
yline(LoA_upper,'--', 'Color', red,  'LineWidth', 1.5, ...
      'DisplayName', sprintf('+1.96 SD = %.2f cm', LoA_upper));
yline(LoA_lower,'--', 'Color', red,  'LineWidth', 1.5, ...
      'DisplayName', sprintf('-1.96 SD = %.2f cm', LoA_lower));
yline(0, ':', 'Color', dgray, 'LineWidth', 1.0, ...
      'DisplayName', 'Zero difference');

scatter(means_BA, signed_error, 70, navy, 'filled', ...
        'MarkerFaceAlpha', 0.85, 'DisplayName', 'Trials');

for i = 1:n
    text(means_BA(i)+0.3, signed_error(i)+0.1, sprintf('T%d', i), ...
         'FontSize', 10, 'Color', dgray);
end

hold off;
xlabel('Mean of Manual and IoT (cm)', 'FontWeight', 'bold');
ylabel('IoT − Manual (cm)',           'FontWeight', 'bold');
title('Bland-Altman Plot: Sit and Reach Agreement', 'FontWeight', 'bold');
legend('Location', 'best');
grid on; box on;
saveas(gcf, 'sr_fig2_bland_altman.png');

%% ── FIGURE 3: Side-by-side bar — all trials ──────────────
figure('Name','Trial Comparison','Position',[100 100 620 420]);
x = 1:n;
bar(x - 0.2, manual,   0.35, 'FaceColor', navy, 'EdgeColor', 'none', ...
    'FaceAlpha', 0.9, 'DisplayName', 'Manual');
hold on;
bar(x + 0.2, acquired, 0.35, 'FaceColor', red,  'EdgeColor', 'none', ...
    'FaceAlpha', 0.9, 'DisplayName', 'IoT Acquired');
hold off;
xlabel('Trial Number',        'FontWeight', 'bold');
ylabel('Reach Distance (cm)', 'FontWeight', 'bold');
title('Manual vs IoT Measurement — All Trials', 'FontWeight', 'bold');
set(gca, 'XTick', 1:n);
legend; grid on; box on; xlim([0 n+1]);
saveas(gcf, 'sr_fig3_trial_comparison.png');

%% ── FIGURE 4: Absolute error per trial ───────────────────
figure('Name','Error per Trial','Position',[100 100 620 400]);
bar(1:n, abs_error, 'FaceColor', navy, 'EdgeColor', 'none', 'FaceAlpha', 0.85);
hold on;
yline(MAE, '--r', 'LineWidth', 2, 'DisplayName', sprintf('MAE = %.2f cm', MAE));
hold off;
xlabel('Trial Number',       'FontWeight', 'bold');
ylabel('Absolute Error (cm)','FontWeight', 'bold');
title('Absolute Error per Trial — Sit and Reach', 'FontWeight', 'bold');
set(gca, 'XTick', 1:n);
legend; grid on; box on; xlim([0 n+1]);
saveas(gcf, 'sr_fig4_error_per_trial.png');

fprintf('✓ All figures saved.\n');
fprintf('✓ Note: with only %d trials, interpret metrics as\n', n);
fprintf('  indicative only — not statistically conclusive.\n');
fprintf('  State this clearly in your report discussion.\n\n');