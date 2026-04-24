clc; clear; close all;

%% ===== LOAD DATA =====
T = readtable("C:\projects\.venv\HILPCS\FLAMYO\test2\flamingo_test_results.csv");

test_names = T.Test;
acquired   = T.Acquired;
actual     = T.Actual;

n = height(T);

%% ===== METRICS =====
error          = acquired - actual;
abs_error      = abs(error);
pct_error      = (abs_error ./ actual) * 100;

MAE            = mean(abs_error);
RMSE           = sqrt(mean(error.^2));
ME             = mean(error);           % mean bias
SD_error       = std(error);
MAPE           = mean(pct_error);

% Pearson correlation
R              = corrcoef(actual, acquired);
r_val          = R(1,2);
r2_val         = r_val^2;

fprintf('============ EVALUATION METRICS ============\n');
fprintf('MAE              : %.3f\n', MAE);
fprintf('RMSE             : %.3f\n', RMSE);
fprintf('Mean Bias (ME)   : %.3f\n', ME);
fprintf('Std of Error     : %.3f\n', SD_error);
fprintf('MAPE             : %.2f%%\n', MAPE);
fprintf('Pearson r        : %.4f\n', r_val);
fprintf('R²               : %.4f\n', r2_val);
fprintf('============================================\n');

%% ===== PLOT 1: ACQUIRED vs ACTUAL BAR CHART =====
figure('Position', [100 100 1000 420]);

x = 1:n;
bar_width = 0.35;

b1 = bar(x - bar_width/2, actual,   bar_width, 'FaceColor', [0.2 0.5 0.8]);
hold on;
b2 = bar(x + bar_width/2, acquired, bar_width, 'FaceColor', [0.9 0.4 0.2]);

set(gca, 'XTick', x, 'XTickLabel', 1:n);
xlabel('Test');
ylabel('Count');
title('Acquired vs Actual Count per Test');
legend([b1 b2], {'Actual', 'Acquired'}, 'Location', 'best');
grid on;


%% ===== PLOT 2: SCATTER + IDENTITY LINE (Correlation) =====
figure('Position', [100 100 520 500]);

scatter(actual, acquired, 60, 'filled', 'MarkerFaceColor', [0.2 0.5 0.8]);
hold on;

% identity line
min_val = min([actual; acquired]) - 1;
max_val = max([actual; acquired]) + 1;
plot([min_val max_val], [min_val max_val], 'k--', 'LineWidth', 1.5);

% linear fit line
p = polyfit(actual, acquired, 1);
x_fit = linspace(min_val, max_val, 100);
y_fit = polyval(p, x_fit);
plot(x_fit, y_fit, 'r-', 'LineWidth', 1.5);




xlabel('Actual');
ylabel('Acquired');
title(sprintf('Correlation — r = %.4f,  R² = %.4f', r_val, r2_val));
legend({'Data Points', 'Identity (y=x)', 'Linear Fit'}, 'Location', 'best');
grid on;
axis equal;
xlim([min_val max_val]);
ylim([min_val max_val]);

%% ===== PLOT 3: BLAND-ALTMAN =====
figure('Position', [100 100 700 480]);

means_ba = (actual + acquired) / 2;
diffs_ba = acquired - actual;

mean_diff = mean(diffs_ba);
sd_diff   = std(diffs_ba);
loa_upper = mean_diff + 1.96 * sd_diff;
loa_lower = mean_diff - 1.96 * sd_diff;

scatter(means_ba, diffs_ba, 60, 'filled', 'MarkerFaceColor', [0.2 0.6 0.4]);
hold on;

yline(mean_diff, 'b-',  'LineWidth', 1.8);
yline(loa_upper, 'r--', 'LineWidth', 1.5);
yline(loa_lower, 'r--', 'LineWidth', 1.5);
yline(0,         'k:',  'LineWidth', 1.2);

% labels on lines
xlims = xlim;
text(xlims(2), mean_diff, sprintf('  Mean = %.2f', mean_diff), ...
    'Color','b', 'FontSize', 9, 'VerticalAlignment','bottom');
text(xlims(2), loa_upper, sprintf('  +1.96SD = %.2f', loa_upper), ...
    'Color','r', 'FontSize', 9, 'VerticalAlignment','bottom');
text(xlims(2), loa_lower, sprintf('  -1.96SD = %.2f', loa_lower), ...
    'Color','r', 'FontSize', 9, 'VerticalAlignment','top');


xlabel('Mean of Actual & Acquired');
ylabel('Difference (Acquired − Actual)');
title('Bland-Altman Plot');
grid on;

%% ===== PLOT 4: ERROR per TEST =====
figure('Position', [100 100 1000 420]);

b_err = bar(x, error, 'FaceColor', 'flat');

for i = 1:n
    if error(i) > 0
        b_err.CData(i,:) = [0.85 0.33 0.10];   % over-estimate → orange
    elseif error(i) < 0
        b_err.CData(i,:) = [0.13 0.47 0.71];   % under-estimate → blue
    else
        b_err.CData(i,:) = [0.47 0.67 0.19];   % exact → green
    end
end

hold on;
yline(0,  'k-',  'LineWidth', 1.2);
yline( 1, 'k:',  'LineWidth', 1);
yline(-1, 'k:',  'LineWidth', 1);

set(gca, 'XTick', x, 'XTickLabel', test_names, 'XTickLabelRotation', 30);
xlabel('Test');
ylabel('Error (Acquired − Actual)');
title('Error per Test  (orange = overcount | blue = undercount | green = exact)');
grid on;

%% ===== PLOT 5: ABSOLUTE ERROR + PERCENTAGE ERROR =====
figure('Position', [100 100 1000 420]);

yyaxis left;
bar(x - bar_width/2, abs_error, bar_width, 'FaceColor', [0.5 0.2 0.7]);
ylabel('Absolute Error');

yyaxis right;
plot(x, pct_error, 'ko-', 'LineWidth', 1.5, 'MarkerFaceColor', 'k');
ylabel('Percentage Error (%)');

set(gca, 'XTick', x, 'XTickLabel', test_names, 'XTickLabelRotation', 30);
xlabel('Test');
title(sprintf('Absolute & Percentage Error per Test  |  MAE=%.2f  MAPE=%.1f%%', MAE, MAPE));
legend({'Absolute Error', 'Percentage Error'}, 'Location', 'best');
grid on;

%% ===== PLOT 6: METRICS SUMMARY TABLE (as figure) =====
figure('Position', [100 100 480 320]);
axis off;

col_names = {'Metric', 'Value'};
table_data = {
    'MAE',              sprintf('%.3f', MAE);
    'RMSE',             sprintf('%.3f', RMSE);
    'Mean Bias (ME)',   sprintf('%.3f', ME);
    'Std of Error',     sprintf('%.3f', SD_error);
    'MAPE',             sprintf('%.2f%%', MAPE);
    'Pearson r',        sprintf('%.4f', r_val);
    'R²',               sprintf('%.4f', r2_val);
};

t_ui = uitable('Data', table_data, ...
               'ColumnName', col_names, ...
               'Units', 'Normalized', ...
               'Position', [0.05 0.05 0.90 0.90], ...
               'FontSize', 11);

title('Evaluation Metrics Summary');