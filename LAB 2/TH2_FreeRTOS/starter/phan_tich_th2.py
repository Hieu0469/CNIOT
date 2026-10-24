#!/usr/bin/env python3
"""phan_tich_th2.py – CE339 Lab TH2: phân tích log Serial (jitter lấy mẫu và đảo ngược ưu tiên)
MÃ KHUNG cho sinh viên: viết các hàm có TODO, điền DU_DOAN từ Phiếu dự đoán của bạn.

Cách dùng:
    python3 phan_tich_th2.py jitter A0.txt B0.txt C0.txt A1.txt B1.txt C1.txt
        (mỗi tệp là log Serial một lần chạy THI_NGHIEM 1; tên tệp tuỳ ý, CHE_DO/TAI_NEN đọc từ dòng '#')
    python3 phan_tich_th2.py daonguoc khoa0.txt khoa1.txt
    python3 phan_tich_th2.py vcd wokwi-logic.vcd C1.txt
        (tệp VCD do logic analyzer của Wokwi ghi khi DỪNG mô phỏng + log Serial của CÙNG lần chạy)
Kết quả: bảng trên màn hình, ket_qua_th2.csv, hinh_th2_jitter.png / hinh_th2_daonguoc.png / hinh_th2_vcd.png

Log Serial chỉ in N_IN = 2000 nhãn đầu (dễ chép từ Serial Monitor). N_10s và độ lệch nhãn cuối của cả cửa sổ 10 s
được đọc từ dòng '# TONG KET 10 s' do ESP32 tính; chuỗi đầy đủ nằm trong tệp VCD.
"""
import sys, re, math
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

# SV điền từ Phiếu dự đoán: (CHE_DO, TAI_NEN) -> (dt trung bình µs, N_10s)
DU_DOAN = {                                  
    ('A', 0): (1550.0, 6451),
    ('B', 0): (1000.0, 10000),
    ('C', 0): (1000.0, 10000),
    ('A', 1): (1573.5, 6355),
    ('B', 1): (1000.0, 9850),
    ('C', 1): (1000.0, 10000),
}
DU_DOAN_CHAN = {0: 112.0, 1: 32.0}            # Semaphore: 112 ms, Mutex: 32 ms


def doc_log(path):
    ts, rows = {}, []
    with open(path, encoding='utf-8', errors='ignore') as f:
        for line in f:
            line = line.strip()
            if line.startswith('#'):
                for k, v in re.findall(r'([A-Za-z_0-9]+)=([A-Za-z0-9.\-]+)', line):
                    ts[k] = v
            elif re.match(r'^\d+,\d+$', line):
                rows.append([int(t) for t in line.split(',')])
            elif re.match(r'^[A-Za-z_]+,[-\d.]+$', line):
                k, v = line.split(',')
                ts['ev_' + k] = float(v)
    return ts, np.array(rows)


# ------------------------------------------------------------------ các hàm SV phải viết (TODO)
def thong_ke_dt(t_us):
    """Từ dãy nhãn thời gian t_us (µs): trả về dict dt_tb, sd, min, max, N_10s."""
    nan = float('nan')
    if len(t_us) < 2:
        return dict(dt_tb=nan, sd=nan, min=nan, max=nan, N_10s=0)
    dt = np.diff(t_us)
    dt_tb = float(np.mean(dt))
    sd = float(np.std(dt, ddof=1)) if len(dt) > 1 else 0.0
    mn = int(np.min(dt))
    mx = int(np.max(dt))
    N_10s = int(np.sum(t_us - t_us[0] < 10_000_000))
    return dict(dt_tb=dt_tb, sd=sd, min=mn, max=mx, N_10s=N_10s)


def lech_luoi(t_us, T0):
    """Độ lệch (µs) của mẫu thứ k so với lưới lý tưởng t[0] + k*T0."""
    if len(t_us) == 0:
        return np.array([])
    k = np.arange(len(t_us))
    return t_us - (t_us[0] + k * T0)


def thoi_gian_chan(ev):
    """Thời gian H bị chặn (ms) từ các sự kiện ev['H_xin_khoa'], ev['H_duoc_khoa']."""
    if 'H_xin_khoa' in ev and 'H_duoc_khoa' in ev:
        return (ev['H_duoc_khoa'] - ev['H_xin_khoa'])
    return float('nan')

# ------------------------------------------------------------------ chương trình chính
def jitter(files):
    fig, ax = plt.subplots(1, 2, figsize=(14, 4.4))
    mau = {'A': '#C0392B', 'B': '#E67E22', 'C': '#1F3A93'}
    out = []
    print(f'{"chạy":<6} {"dt_tb":>9} {"sd":>8} {"min":>7} {"max":>7} {"N_10s":>6} \
    {"lệch cuối (ms)":>15}   so với dự đoán')
    for p in files:
        ts, d = doc_log(p)
        mode, load, T0 = ts.get('CHE_DO', '?'), int(ts.get('TAI_NEN', 0)), int(ts.get('T0_US', 1000))
        t = d[:, 1]
        r = thong_ke_dt(t)                     # thống kê trên các nhãn ĐÃ IN (tối đa 2000 nhãn đầu)
        e = lech_luoi(t, T0)
        if 'N_10s' in ts:                      # cả cửa sổ 10 s: lấy từ dòng TONG KET do ESP32 tính
            r['N_10s'] = int(ts['N_10s'])
        lech_cuoi = int(ts['lech_cuoi_us']) if 'lech_cuoi_us' in ts else e[-1]
        dd = DU_DOAN.get((mode, load))
        cmp = ''
        if dd:
            l1 = (r['dt_tb'] - dd[0]) / dd[0] * 100
            l2 = (r['N_10s'] - dd[1]) / dd[1] * 100
            cmp = f'dt {l1:+.1f}%, N {l2:+.1f}%' + ('  <-- lệch > 20%' if max(abs(l1), abs(l2)) > 20 else '')
        print(f'{mode}{load:<5} {r["dt_tb"]:>9.1f} \
        {r["sd"]:>8.1f} {r["min"]:>7} {r["max"]:>7} {r["N_10s"]:>6} {lech_cuoi / 1000:>15.1f}   {cmp}')
        out.append((f'{mode}{load}', r, lech_cuoi / 1000))
        dt = np.diff(t)
        ax[0].plot(np.arange(len(e)), e / 1000, color=mau.get(mode, 'gray'), ls='-' if load == 0 else '--', lw=1.5,
                   label=f'{mode}, tải={load}')
        ax[1].hist(dt, bins=np.arange(0, dt.max() + 100, 50), histtype='step', lw=1.5, color=mau.get(mode, 'gray'),
                   ls='-' if load == 0 else '--', label=f'{mode}, tải={load}')
    ax[0].set_xlabel('chỉ số mẫu k') 
    ax[0].set_ylabel('t_k − t_0 − k·T0 (ms)')
    ax[0].set_title('Lệch so với lưới lý tưởng')
    ax[1].set_yscale('log') 
    ax[1].set_xlabel('khoảng cách hai mẫu dt (µs)') 
    ax[1].set_ylabel('số lần (log)')
    ax[1].set_title('Phân bố dt')
    for a in ax:
        a.legend(fontsize=9)
        a.spines['top'].set_visible(False)
        a.spines['right'].set_visible(False)
    fig.tight_layout()
    fig.savefig('hinh_th2_jitter.png', dpi=130)
    with open('ket_qua_th2.csv', 'w', encoding='utf-8') as f:
        f.write('chay,dt_tb,sd,min,max,N_10s,lech_cuoi_ms\n')
        for name, r, lc in out:
            f.write(f'{name},{r["dt_tb"]:.2f},{r["sd"]:.2f},{r["min"]},{r["max"]},{r["N_10s"]},{lc:.2f}\n')
    print('(dt_tb, sd, min, max: trên các nhãn đã in; N_10s và lệch cuối: cả cửa sổ 10 s, từ dòng TONG KET)')
    print('Đã lưu ket_qua_th2.csv, hinh_th2_jitter.png')


# ------------------------------------------------------------------ logic analyzer (VCD) – cho sẵn
def doc_vcd(path, kenh='D0'):
    """Đọc tệp VCD (wokwi-logic.vcd): trả về mảng thời điểm (µs, số thực) của mọi lần đổi mức trên kênh `kenh`,
    bỏ giá trị khởi tạo ở thời điểm 0."""
    don_vi = {'s': 1e6, 'ms': 1e3, 'us': 1.0, 'ns': 1e-3, 'ps': 1e-6, 'fs': 1e-9}
    scale, ident, t, cuoi, canh = 1e-3, None, 0, None, []
    with open(path, encoding='utf-8', errors='ignore') as f:
        noi_dung = f.read()
    m = re.search(r'\$timescale\s+(\d+)\s*([munpf]?s)\s+\$end', noi_dung)
    if m:
        scale = int(m.group(1)) * don_vi[m.group(2)]
    for m in re.finditer(r'\$var\s+\S+\s+\d+\s+(\S+)\s+(\S+)', noi_dung):
        if m.group(2) == kenh:
            ident = m.group(1)
    if ident is None:
        raise SystemExit(f'Không thấy kênh {kenh} trong {path}')
    than = noi_dung.split('$enddefinitions', 1)[-1]
    for tok in than.split():
        if tok.startswith('#'):
            t = int(tok[1:])
        elif tok[1:] == ident and tok[0] in '01xXzZ':
            if cuoi is not None and tok[0] != cuoi and t > 0:
                canh.append(t * scale)
            cuoi = tok[0]
    return np.array(canh)


def vcd(files):
    """So sánh chu kỳ đo bằng logic analyzer (cạnh GPIO18) với số liệu micros() trong log Serial cùng lần chạy."""
    canh = doc_vcd(files[0])
    ts = doc_log(files[1])[0] if len(files) > 1 else {}
    T0 = int(ts.get('T0_US', 0))
    # Mẫu seq = 0 ghi mức 0 (chưa đổi mức) -> cạnh thứ j là mẫu seq = j + 1; log bỏ 5 mẫu đầu -> bỏ 4 cạnh đầu
    t = canh[4:]
    t = t[t - t[0] < 10_000_000]                                 # cùng cửa sổ 10 s như ESP32
    r = thong_ke_dt(np.round(t).astype(np.int64))
    print(f'VCD {files[0]}: {len(canh)} cạnh; cửa sổ 10 s: N = {r["N_10s"]}, dt_tb = {r["dt_tb"]:.2f}, '
          f'sd = {r["sd"]:.2f}, min = {r["min"]}, max = {r["max"]} µs')
    if T0:
        e = lech_luoi(np.round(t).astype(np.int64), T0)
        print(f'  lệch lưới: nhãn cuối {e[-1] / 1000:.2f} ms, |lệch| lớn nhất {np.abs(e).max():.0f} µs (T0 = {T0} µs)')
    if 'N_10s' in ts:
        print(f'ESP32 (micros, TONG KET): N = {ts["N_10s"]}, dt_tb = {ts.get("dt_tb")}, sd = {ts.get("sd")}, '
              f'min = {ts.get("min")}, max = {ts.get("max")} µs')
    dt = np.diff(t)
    fig, ax = plt.subplots(figsize=(7, 3.6))
    ax.hist(dt, bins=np.arange(0, dt.max() + 100, 50), histtype='step', lw=1.5, color='#1F3A93')
    ax.set_yscale('log')
    ax.set_xlabel('khoảng cách hai cạnh GPIO18 (µs)')
    ax.set_ylabel('số lần (log)')
    ax.set_title('Chu kỳ lấy mẫu đo bằng logic analyzer (VCD)')
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)
    fig.tight_layout()
    fig.savefig('hinh_th2_vcd.png', dpi=130)
    print('Đã lưu hinh_th2_vcd.png')


def daonguoc(files):
    fig, axs = plt.subplots(len(files), 1, figsize=(12, 2.2 * len(files)), sharex=True, squeeze=False)
    for ax, p in zip(axs[:, 0], files):
        ts, _ = doc_log(p)
        ev = {k[3:]: v for k, v in ts.items() if k.startswith('ev_')}
        khoa = int(ts.get('KHOA', -1)) if str(ts.get('KHOA', '')).lstrip('-').isdigit() else -1
        chan = thoi_gian_chan(ev)
        du = DU_DOAN_CHAN.get(khoa)
        print(f'KHOA={khoa}: H bị chặn {chan:.2f} ms' + \
        (f' (dự đoán {du} ms, lệch {(chan - du) / du * 100:+.1f}%)' if du else ''))
        bars = [('H', ev['H_bat_dau'], ev['H_xin_khoa'], '#C0392B', 'H chạy'),
                ('H', ev['H_xin_khoa'], ev['H_duoc_khoa'], '#F5B7B1', 'H chờ khoá'),
                ('H', ev['H_duoc_khoa'], ev['H_xong'], '#C0392B', ''),
                ('M', ev['M_bat_dau'], ev['M_xong'], '#3366CC', 'M chạy'),
                ('L', ev['L_lay_khoa'], ev['L_nha_khoa'], '#7F8C8D', 'L giữ khoá')]
        rows = {'H': 2, 'M': 1, 'L': 0}
        for r, a, b, c, lab in bars:
            ax.barh(rows[r], b - a, left=a, color=c, edgecolor='black', height=0.6)
            if lab:
                ax.text((a + b) / 2, rows[r], lab, ha='center', va='center', fontsize=8)
        ax.set_yticks([0, 1, 2])
        ax.set_yticklabels(['L (2)', 'M (3)', 'H (4)'])
        ax.set_title(f'KHOA = {"mutex" if khoa == 1 \
        else "semaphore nhị phân" if khoa == 0 else "?"}: H bị chặn {chan:.1f} ms',
                     loc='left', fontsize=11)
        ax.spines['top'].set_visible(False)
        ax.spines['right'].set_visible(False)
    axs[-1, 0].set_xlabel('t (ms), mốc 0 = lúc L bắt đầu')
    fig.tight_layout()
    fig.savefig('hinh_th2_daonguoc.png', dpi=130)
    print('Đã lưu hinh_th2_daonguoc.png')


if __name__ == '__main__':
    if len(sys.argv) < 3 or sys.argv[1] not in ('jitter', 'daonguoc', 'vcd'):
        print(__doc__)
        sys.exit(1)
    {'jitter': jitter, 'daonguoc': daonguoc, 'vcd': vcd}[sys.argv[1]](sys.argv[2:])
