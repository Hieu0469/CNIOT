#!/usr/bin/env python3
"""phan_tich_th1.py – CE339 Lab TH1: phân tích dữ liệu Serial từ ESP32 (Wokwi)
MÃ KHUNG cho sinh viên: viết các hàm có TODO, điền DU_DOAN từ Phiếu dự đoán của bạn.

Cách dùng:
    python3 phan_tich_th1.py che_do1.txt che_do2.txt che_do3.txt
    (mỗi tệp là toàn bộ nội dung Serial Monitor của một lần chạy CHE_DO 1, 2, 3 – chép và lưu thành .txt)
Kết quả: bảng so sánh đo/dự đoán trên màn hình, ket_qua_th1.csv, hinh_th1.png
"""
#!/usr/bin/env python3
"""phan_tich_th1.py – CE339 Lab TH1: phân tích dữ liệu Serial từ ESP32 (Wokwi)
"""
import sys, re, math
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

# ------------------------------------------------------------------ SV điền từ Phiếu dự đoán
DU_DOAN = {                      
    # Các tham số được tính với d=7, c=4 (MSSV 23520447)
    'sigma_x': 11.0, 
    'ENOB': 6.75,
    'sigma_ma': 4.16,      # 11 / sqrt(7)
    'sigma_ema': 3.67,     # 11 * sqrt(0.2 / 1.8)
    'sigma_med': 6.15,     # 1.25 * 11 / sqrt(5)
    'd50_ma': 3.0,         # (7 - 1) / 2
    'd50_ema': 2.1,        # ln(0.5)/ln(1-0.2) - 1
    'd50_med': 2.0,        # (5 - 1) / 2
    'xung_tho': 8.0,       # 8 xung trong vùng n=50 đến 999
    'xung_ma': 56.0,       # 8 * 7 mẫu bị ảnh hưởng
    'xung_ema': 48.0,      # 8 * ~6 mẫu bị ảnh hưởng vượt ngưỡng
    'xung_med': 0.0,       # Lọc trung vị loại bỏ hoàn toàn
}
BO_QUA = 50                      # bỏ 50 mẫu đầu (khởi động bộ lọc)


def doc_log(path):
    """Đọc log Serial: trả về (tham_so: dict, du_lieu: ndarray cột n,x,ma,ema,med)."""
    ts, rows = {}, []
    with open(path, encoding='utf-8', errors='ignore') as f:
        for line in f:
            line = line.strip()
            if line.startswith('#'):
                for k, v in re.findall(r'([A-Z_0-9]+)=([-\d.]+)', line):
                    ts[k] = float(v)
            elif re.match(r'^\d+,', line):
                p = line.split(',')
                if len(p) == 5:
                    rows.append([float(t) for t in p])
    return ts, np.array(rows)


# ------------------------------------------------------------------ các hàm SV phải viết (TODO)
def thong_ke(x):
    """Trả về (trung bình, độ lệch chuẩn mẫu ddof=1)."""
    # Dùng numpy để tính trung bình và độ lệch chuẩn mẫu (ddof=1)
    if len(x) == 0:
        return float("nan"), float("nan")
    return np.mean(x), np.std(x, ddof=1)


def enob(sigma_lsb, nbits=12):
    """ENOB ≈ N − log2(σ·√12)  (slide 'ENOB: ADC 12 bit thực chất được bao nhiêu bit?')."""
    if sigma_lsb <= 0:
        return float(nbits)
    return float(nbits - math.log2(sigma_lsb * math.sqrt(12.0)))


def do_tre_d50(y, n0, nen, S):
    """Số mẫu từ n0 tới mẫu ĐẦU TIÊN có y >= nen + S/2 (đầu vào là bậc thang không nhiễu)."""
    nguong = nen + S / 2.0
    # Chuyển đổi n0 thành index nguyên để duyệt mảng
    n0 = int(n0)
    for i in range(n0, len(y)):
        if y[i] >= nguong:
            return float(i - n0)
    return -1.0


def dem_lech(y, mu, nguong):
    """Đếm số mẫu có |y − mu| > nguong."""
    # numpy vectorization giúp đếm nhanh số phần tử vượt ngưỡng
    return float(np.sum(np.abs(y - mu) > nguong))


# ------------------------------------------------------------------ chương trình chính
def so_sanh(ten, do, du):
    if du is None or (isinstance(du, float) and math.isnan(du)) or (isinstance(do, float) and math.isnan(do)):
        return f'{ten:<12} {do:>10} {"?":>10}'
    lech = (do - du) / du * 100 if du != 0 else (0.0 if do == 0 else float('inf'))
    canh_bao = '  <-- lệch > 20%, PHẢI giải thích' if abs(lech) > 20 else ''
    return f'{ten:<12} {do:>10.3f} {du:>10.3f} {lech:>8.1f}%{canh_bao}'


def main(argv):
    if len(argv) < 4:
        print(__doc__); return 1
    ts1, d1 = doc_log(argv[1]); ts2, d2 = doc_log(argv[2]); ts3, d3 = doc_log(argv[3])
    for name, d in (('che_do1', d1), ('che_do2', d2), ('che_do3', d3)):
        if len(d) < 1000:
            print(f'Cảnh báo: {name} chỉ có {len(d)} dòng dữ liệu (cần 1000)')
    x1 = d1[BO_QUA:, 1]; ma1 = d1[BO_QUA:, 2]; ema1 = d1[BO_QUA:, 3]; med1 = d1[BO_QUA:, 4]
    mu, sx = thong_ke(x1)
    kq = {'mean_x': mu, 'sigma_x': sx, 'ENOB': enob(sx),
          'sigma_ma': thong_ke(ma1)[1], 'sigma_ema': thong_ke(ema1)[1], 'sigma_med': thong_ke(med1)[1]}
    # Chế độ 3: bậc thang không nhiễu
    n0 = int(ts3.get('N0', 500)); S = ts3.get('STEP', 400); nen = d3[n0 - 1, 1]
    kq['d50_ma'] = do_tre_d50(d3[:, 2], n0, nen, S)
    kq['d50_ema'] = do_tre_d50(d3[:, 3], n0, nen, S)
    kq['d50_med'] = do_tre_d50(d3[:, 4], n0, nen, S)
    # Chế độ 2: xung nhiễu – ngưỡng 5σ lấy từ chế độ 1
    nguong = 5 * sx
    kq['xung_tho'] = dem_lech(d2[BO_QUA:, 1], mu, nguong)
    kq['xung_ma'] = dem_lech(d2[BO_QUA:, 2], mu, nguong)
    kq['xung_ema'] = dem_lech(d2[BO_QUA:, 3], mu, nguong)
    kq['xung_med'] = dem_lech(d2[BO_QUA:, 4], mu, nguong)

    print(f"Tham số: N_MA={ts1.get('N_MA')}, ALPHA={ts1.get('ALPHA')}, SIGMA_N={ts1.get('SIGMA_N')}, "
          f"K_SPIKE={ts2.get('K_SPIKE')}, mean_x={mu:.2f} LSB")
    print(f'{"Đại lượng":<12} {"Đo":>10} {"Dự đoán":>10} {"Lệch":>9}')
    for k in ['sigma_x', 'ENOB', 'sigma_ma', 'sigma_ema', 'sigma_med', 'd50_ma', 'd50_ema', 'd50_med',
              'xung_tho', 'xung_ma', 'xung_ema', 'xung_med']:
        print(so_sanh(k, kq[k], DU_DOAN.get(k)))
    with open('ket_qua_th1.csv', 'w', encoding='utf-8') as f:
        f.write('dai_luong,do,du_doan\n')
        for k, v in kq.items():
            f.write(f'{k},{v},{DU_DOAN.get(k, "")}\n')

    # ---- Hình: histogram, xung nhiễu, bậc thang
    fig, ax = plt.subplots(1, 3, figsize=(15, 4.2))
    ax[0].hist(x1, bins=np.arange(x1.min() - 0.5, x1.max() + 1.5, max(1, int((x1.max() - x1.min()) // 30) + 1)), density=True, color='#3366CC', alpha=0.75, label='x (CHE_DO 1)')
    if sx > 0:
        g = np.linspace(x1.min(), x1.max(), 200)
        ax[0].plot(g, np.exp(-(g - mu) ** 2 / (2 * sx ** 2)) / (sx * math.sqrt(2 * math.pi)), color='#E67E22', lw=2, label='Gauss(μ, σ)')
    ax[0].set_title('Histogram nhiễu'); ax[0].set_xlabel('mã ADC (LSB)'); ax[0].legend()
    k = int(ts2.get('K_SPIKE', 100)); a, b = BO_QUA, min(BO_QUA + 3 * k, len(d2))
    for j, (lab, c) in enumerate([('thô', '#BBBBBB'), ('MA', '#1F3A93'), ('EMA', '#E67E22'), ('trung vị', '#27AE60')]):
        ax[1].plot(d2[a:b, 0], d2[a:b, j + 1], color=c, lw=1.2 if j else 0.8, label=lab)
    ax[1].set_title('Xung nhiễu (CHE_DO 2)'); ax[1].set_xlabel('n'); ax[1].legend()
    a, b = n0 - 10, n0 + 25
    for j, (lab, c) in enumerate([('vào', '#BBBBBB'), ('MA', '#1F3A93'), ('EMA', '#E67E22'), ('trung vị', '#27AE60')]):
        ax[2].step(d3[a:b, 0], d3[a:b, j + 1], where='post', color=c, lw=2 if j else 1, label=lab)
    ax[2].axhline(nen + S / 2, color='#C0392B', ls='--', lw=1); ax[2].set_title('Bậc thang (CHE_DO 3)')
    ax[2].set_xlabel('n'); ax[2].legend()
    for a_ in ax:
        a_.spines['top'].set_visible(False); a_.spines['right'].set_visible(False)
    fig.tight_layout(); fig.savefig('hinh_th1.png', dpi=130)
    print('Đã lưu ket_qua_th1.csv, hinh_th1.png')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))