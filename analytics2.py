import pandas as pd
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy.signal import butter, filtfilt, find_peaks, windows, savgol_filter
from scipy.ndimage import gaussian_filter1d
import base64


def run_analytics2(file_path1: str, file_path2: str) -> str:
    results = []



    T1 = pd.read_csv(file_path1)
    T2 = pd.read_csv(file_path2)

    vars = T1.columns.tolist()
    signals = ['s1', 's2', 's3', 's4']

    # --- Time ---
    t1 = T1[vars[0]].values
    t2 = T2[vars[0]].values

    cv1 = 0
    cv2 = 0

    for sig in signals:
        x1 = T1[sig].values
        x2 = T2[sig].values

        cv1 += np.std(x1, ddof=0) / abs(np.mean(x1))
        cv2 += np.std(x2, ddof=0) / abs(np.mean(x2))

    # --- Average CV ---
    cv1 = cv1 / len(signals)
    cv2 = cv2 / len(signals)


    if cv1 < cv2:
        results.append('Right leg was up')
        T_stable = T2
        name = 'R2'
    else:
        results.append('Left leg was up')
        T_stable = T1
        name = 'L2'


    # --- Equivalent of nested max(max(max(...))) ---
    avg_signal = np.maximum.reduce([
        T_stable['s1'].values,
        T_stable['s2'].values,
        T_stable['s3'].values,
        T_stable['s4'].values
    ])

    t = T_stable['Time'].values

    ax = T_stable['Ax'].values
    az = T_stable['Az'].values

    t = T_stable['Time'].values   # already in seconds (same as MATLAB)

    # --- dt ---
    dt = np.mean(np.diff(t))

    # --- Derivatives ---
    dax = np.diff(ax) / dt
    daz = np.diff(az) / dt

    t_deriv = t[:-1]   # match diff size


    fs = 43
    fc = 2

    b, a = butter(4, fc/(fs/2), btype='low')

    dax_filt = filtfilt(b, a, dax)
    daz_filt = filtfilt(b, a, daz)


    # ===== TRIANGULAR SMOOTHING =====
    window = 10

    tri_window = windows.triang(window)
    tri_window = tri_window / np.sum(tri_window)

    dax_smooth = np.convolve(dax_filt, tri_window, mode='same')
    daz_smooth = np.convolve(daz_filt, tri_window, mode='same')

    # ===== ADAPTIVE PROMINENCE =====
    k = 2

    std_ax = np.std(dax_smooth, ddof=0)
    std_az = np.std(daz_smooth, ddof=0)

    prom_ax = k * std_ax
    prom_az = k * std_az

    # ===== PEAK DETECTION =====
    min_dist = 30   # seconds

    # convert distance to samples (IMPORTANT difference!)
    min_dist_samples = int(min_dist / dt)

    pks_ax, _ = find_peaks(dax_smooth,
                        prominence=prom_ax,
                        distance=min_dist_samples)

    pks_az, _ = find_peaks(daz_smooth,
                        prominence=prom_az,
                        distance=min_dist_samples)

    # --- Convert indices → time ---
    locs_ax = t_deriv[pks_ax]
    locs_az = t_deriv[pks_az]

    pks_ax_vals = dax_smooth[pks_ax]
    pks_az_vals = daz_smooth[pks_az]


    # ===== CHECK =====
    if len(pks_ax_vals) == 0 or len(pks_az_vals) == 0:
        raise ValueError('No peaks detected — reduce k value!')


    # ===== FIRST PEAK =====
    first_peak_ax = pks_ax_vals[0]
    time_ax = locs_ax[0]

    first_peak_az = pks_az_vals[0]
    time_az = locs_az[0]


    # ===== TIME-BASED SELECTION =====
    if time_ax > time_az:
        selected_time = time_ax
        selected_peak = first_peak_ax
        selected_axis = 'Ax'
    else:
        selected_time = time_az
        selected_peak = first_peak_az
        selected_axis = 'Az'

    # ===== EARLY EXIT CHECK =====
    # ===== EARLY EXIT CHECK =====
    idx_start_check = np.where(t >= selected_time)[0][0]
    avg_from_start = avg_signal[idx_start_check:]

    if np.max(avg_from_start) < 160:
        plt.figure(figsize=(14, 5))
        plt.plot(t, avg_signal, 'k', linewidth=1.5)
        plt.axvline(selected_time, linestyle='--', color='r', linewidth=1.5, label='Onset')
        plt.axhline(160, linestyle='--', color='b', linewidth=1.2, label='Threshold (160)')
        plt.title('Flamingo Test Imbalance Detection', fontsize=14, fontweight='bold')
        plt.xlabel('Time (s)')
        plt.ylabel('Signal Amplitude')
        plt.legend(loc='upper right', bbox_to_anchor=(1.0, 1.0), framealpha=0.9)
        plt.text(t[int(len(t)*0.1)], np.max(avg_signal)*0.9,
                 'No imbalances found',
                 fontsize=12, fontweight='bold', color='b')
        plt.grid(True)
        plt.tight_layout()
        plt.savefig("plot_matlab.png", dpi=120, bbox_inches='tight')
        plt.close()

        results.append('No imbalances found — signal never exceeded 160 after onset.')
        return "\n".join(results)

    # ===== EXTRACT =====
    Ax = T_stable['Ax'].values

    # ===== TIME & FS =====
    t = T_stable['Time'].values
    fs = 1000 / np.mean(np.diff(t))   # identical to MATLAB

    # ===== LOW PASS FILTER (1 Hz) =====
    fc = 1
    b, a = butter(4, fc/(fs/2), btype='low')
    Ax_filt = filtfilt(b, a, Ax)

    # ===== SAVITZKY-GOLAY =====
    sgolay_order = 3
    frame_len = 51
    Ax_smooth = savgol_filter(Ax_filt, frame_len, sgolay_order)

    # ===== MIDLINE =====
    mid_val = (np.max(Ax_smooth) + np.min(Ax_smooth)) / 2
    mean_val = np.mean(Ax_smooth)
    # ===== FIRST DERIVATIVE (ms-based) =====
    dt = np.mean(np.diff(t))
    dAx = np.diff(Ax_smooth) / dt
    t_deriv = t[:-1]
    # ===== DERIVATIVE (per second) =====
    dt = np.mean(np.diff(t)) / 1000
    dAx = np.diff(Ax_smooth) / dt
    t_deriv = t[:-1]

    # ===== TRIANGULAR SMOOTH =====
    win = 51

    tri_win = windows.triang(win)
    tri_win = tri_win / np.sum(tri_win)

    dAx_smooth = np.convolve(dAx, tri_win, mode='same')

    # ===== SIGMA THRESHOLD =====
    sigma = np.std(dAx_smooth, ddof=0)

    pos_th = 0.8 * sigma
    neg_th = -0.8 * sigma


    # ===== PEAKS =====
    pks_idx, _ = find_peaks(dAx_smooth, height=pos_th)
    locs_p = t_deriv[pks_idx]
    pks = dAx_smooth[pks_idx]

    # ===== VALLEYS =====
    vals_idx, _ = find_peaks(-dAx_smooth, height=sigma)
    locs_v = t_deriv[vals_idx]
    vals = -dAx_smooth[vals_idx]


    # ===== PAIRING =====
    pairs_p = []
    pairs_v = []

    # --- start after onset ---
    i_candidates = np.where(locs_p >= selected_time)[0]
    j_candidates = np.where(locs_v >= selected_time)[0]

    if len(i_candidates) == 0 or len(j_candidates) == 0:
        raise ValueError('No peaks or valleys after onset')

    i = i_candidates[0]
    j = j_candidates[0]

    while i < len(locs_p) and j < len(locs_v):

        if locs_p[i] < locs_v[j]:

            # all peaks before this valley
            peak_group_idx = np.where(locs_p[i:] < locs_v[j])[0]

            if len(peak_group_idx) == 0:
                break

            last_idx = peak_group_idx[-1] + i
            peak_group = locs_p[i:last_idx+1]

            selected_peak = peak_group[0]

            pairs_p.append(selected_peak)
            pairs_v.append(locs_v[j])

            # move i after valley
            next_i = np.where(locs_p > locs_v[j])[0]
            if len(next_i) == 0:
                break

            i = next_i[0]
            j += 1

        else:
            j += 1


    pairs_p = np.array(pairs_p)
    pairs_v = np.array(pairs_v)


    # ===== ENVELOPE =====
    win = 20

    # movmean(abs(avg_signal), win)
    mov_avg = np.convolve(np.abs(avg_signal),
                        np.ones(win)/win,
                        mode='same')

    gauss_win = 60
    sigma = gauss_win / 4

    g = windows.gaussian(gauss_win, std=sigma)
    g = g / np.sum(g)

    envelope = np.convolve(mov_avg, g, mode='same')


    # ===== START INDEX =====
    idx_start = np.where(t >= selected_time)[0][0]

    t_seg = t[idx_start:]
    env_seg = envelope[idx_start:]



    fs = 42
    win_sec = 5
    win_len = int(round(win_sec * fs))

    overlap = 0.75
    step = int(round(win_len * (1 - overlap)))

    thres = 50

    pks_all = []
    locs_all = []

    th_times = []
    th_vals  = []


    # ===== WINDOW LOOP =====
    for start_idx in range(idx_start, len(envelope), step):

        end_idx = start_idx + win_len

        if end_idx > len(envelope):
            end_idx = len(envelope)

        if (end_idx - start_idx) < int(round(0.3 * win_len)):
            continue

        env_win = envelope[start_idx:end_idx]
        t_win = t[start_idx:end_idx]

        env_max = np.max(env_win)
        env_min = np.min(env_win)
        mid_val = (env_max + env_min) / 2

        if mid_val < thres:
            continue

        t_center = np.mean(t_win)

        th_times.append(t_center)
        th_vals.append(mid_val)


    th_times = np.array(th_times)
    th_vals  = np.array(th_vals)


    # ===== THRESHOLD CURVE =====
    if len(th_times) >= 4:
        p = np.polyfit(th_times, th_vals, 3)
        threshold_curve = np.polyval(p, t)
    else:
        results.append('Not Enough Data')
        threshold_curve = np.mean(th_vals) * np.ones_like(t)
        p = np.polyfit(t, threshold_curve, 1)


    # ===== ZERO-CROSSING BASED DISTANCE =====
    sig_centered = envelope - threshold_curve

    cross_idx = np.where(np.diff(np.sign(sig_centered)) != 0)[0]
    t_cross = t[cross_idx]

    if len(t_cross) >= 2:
        distances = np.diff(t_cross)
        min_dist = np.median(distances)
    else:
        min_dist = 0.5



    # ===== PEAK DETECTION =====
    dt = np.mean(np.diff(t))
    min_dist_samples = max(1, int(min_dist / dt))

    pks_idx, _ = find_peaks(envelope, distance=min_dist_samples)

    locs_all = t[pks_idx]
    pks_all  = envelope[pks_idx]


    # ===== UNIQUE =====
    locs_all, unique_idx = np.unique(locs_all, return_index=True)
    pks_all = pks_all[unique_idx]


    # ===== THRESHOLD FILTER =====
    th_at_peaks = np.polyval(p, locs_all)

    valid_idx = (locs_all >= selected_time) & (pks_all >= th_at_peaks)

    pks  = pks_all[valid_idx]
    locs = locs_all[valid_idx]

    num_peaks = len(pks)



    final_locs = []
    final_pks  = []

    num_pairs = len(pairs_v)

    # ---- MAIN LOOP ----
    for i in range(num_pairs - 1):

        t_start = pairs_v[i]
        t_end   = pairs_p[i+1]

        # select peaks in this window
        idx = (locs >= t_start) & (locs <= t_end)

        locs_seg = locs[idx]
        pks_seg  = pks[idx]

        if len(pks_seg) == 0:
            continue

        if len(pks_seg) == 1:
            # only one → keep it
            final_locs.append(locs_seg[0])
            final_pks.append(pks_seg[0])
        else:
            # multiple → keep strongest
            ind = np.argmax(pks_seg)
            final_locs.append(locs_seg[ind])
            final_pks.append(pks_seg[ind])


    # ---- LAST REGION (after last valley) ----
    if len(pairs_v) > 0:

        t_start = pairs_v[-1]
        t_end   = t[-1]

        idx = (locs >= t_start) & (locs <= t_end)

        locs_seg = locs[idx]
        pks_seg  = pks[idx]

        if len(pks_seg) > 0:

            if len(pks_seg) == 1:
                final_locs.append(locs_seg[0])
                final_pks.append(pks_seg[0])
            else:
                ind = np.argmax(pks_seg)
                final_locs.append(locs_seg[ind])
                final_pks.append(pks_seg[ind])


    # ---- KEEP PEAKS BEFORE FIRST VALLEY ----
    if len(pairs_v) > 0:

        idx = locs < pairs_v[0]

        final_locs = list(locs[idx]) + final_locs
        final_pks  = list(pks[idx])  + final_pks


    # ---- CONVERT TO NUMPY ----
    final_locs = np.array(final_locs)
    final_pks  = np.array(final_pks)


    # ===== COUNT =====
    num_final = len(final_locs)

    if num_final == 0:
        results.append('No Imbalances')
        txt_final = 'No imbalances'
    else:
        results.append("Number of imbalances is " + str(num_final))
        txt_final = f'Imbalances: {num_final}'

    # ===== MAIN PLOT (imbalances detected) =====
    fig, ax = plt.subplots(figsize=(16, 6))   # wide + tall

    ax.plot(t, envelope, 'b', linewidth=1.2, label='Envelope')
    ax.plot(t, threshold_curve, '--m', linewidth=2, label='Threshold Curve')

    if len(cross_idx) > 0:
        ax.plot(t_cross, threshold_curve[cross_idx], 'ro', markersize=6, label='Crossings')

    if len(locs) > 0:
        ax.plot(locs, pks, 'gs', markersize=8, label='All Peaks')

    if len(final_locs) > 0:
        ax.plot(final_locs, final_pks, 'rd', markersize=10, label='Final Peaks')

    ax.axvline(selected_time, linestyle='--', color='r', linewidth=1.5, label='Start')

    ax.set_title('Flamingo Test Imbalance Detection', fontsize=14, fontweight='bold')
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Amplitude')
    ax.grid(True)

    # legend pushed OUTSIDE the plot area (no overlap ever)
    ax.legend(loc='upper left',
              bbox_to_anchor=(1.01, 1.0),
              borderaxespad=0,
              framealpha=0.9,
              fontsize=10)

    # imbalance count text — top left corner, clear of legend
    ax.text(0.02, 0.92, txt_final,
            transform=ax.transAxes,    # axes-relative coords (0–1)
            fontsize=12, fontweight='bold', color='r',
            verticalalignment='top')

    plt.tight_layout()
    plt.savefig("plot_matlab.png", dpi=120, bbox_inches='tight')  # bbox_inches keeps legend visible
    plt.close()

    # ===== GROUND CONTACT % =====
    idx_start_final = np.where(t >= selected_time)[0][0]
    avg_from_start_final = avg_signal[idx_start_final:]

    total_samples = len(avg_from_start_final)
    above_160 = np.sum(avg_from_start_final >= 160)

    pct_contact = (above_160 / total_samples) * 100

    results.append(f'{pct_contact:.1f}% of the time foot was touching the ground (signal >= 160 from onset).')

    return "\n".join(results)
    
    
