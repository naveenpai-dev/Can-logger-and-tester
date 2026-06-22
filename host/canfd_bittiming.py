# CAN FD bit-timing solver for C2000 MCAN. Prints BRP/TSEG1/TSEG2/SJW per phase.
def solve(fcan, bitrate, sp, brp_max, tseg1_max, tseg2_max, sjw_max):
    best=None
    for brp in range(1, brp_max+1):
        tqt = round(fcan/(bitrate*brp))
        if tqt < 4: continue
        ach = fcan/(brp*tqt); err=(ach-bitrate)/bitrate
        tseg1 = round(sp*tqt)-1; tseg2 = tqt-1-tseg1
        if tseg1<1 or tseg2<1 or tseg1>tseg1_max or tseg2>tseg2_max: continue
        spach=(1+tseg1)/tqt; sjw=min(tseg2, sjw_max)
        cand=(abs(err), abs(spach-sp), brp, dict(brp=brp,tq=tqt,tseg1=tseg1,tseg2=tseg2,sjw=sjw,bitrate=ach,sp=spach,err=err))
        if best is None or cand[:3]<best[:3]: best=cand
    return best[3] if best else None

for fcan in (40e6, 80e6, 120e6):
    print(f"\n=== MCAN clock = {fcan/1e6:.0f} MHz ===")
    nom = solve(fcan, 2e6, 0.80, 512, 256, 128, 128)   # nominal (arbitration) 2 Mbps
    dat = solve(fcan, 8e6, 0.80, 32, 32, 16, 16)       # data 8 Mbps
    for label,r in (("NOMINAL 2Mbps",nom),("DATA    8Mbps",dat)):
        if r: print(f"  {label}: BRP={r['brp']} TSEG1={r['tseg1']} TSEG2={r['tseg2']} SJW={r['sjw']}"
                     f"  -> {r['bitrate']/1e6:.3f} Mbps, SP {r['sp']*100:.1f}%, err {r['err']*100:+.2f}%")
        else: print(f"  {label}: no clean solution")
