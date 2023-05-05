// CIE 1931 Wavelength Table
#pragma once

#include <promeki/ciepoint.h>
#include <promeki/xyzcolor.h>

namespace promeki {

struct CIEWavelength {
	double          wavelength;
	XYZColor        xyz;
	CIEPoint        xy;
};

static const CIEWavelength cieWavelengthTable[] = {
	{ 360.0, { 0.175560, 0.005290, 0.819150 }, { 0.175560, 0.005290 } },
	{ 361.0, { 0.175480, 0.005290, 0.819230 }, { 0.175480, 0.005290 } },
	{ 362.0, { 0.175400, 0.005280, 0.819320 }, { 0.175400, 0.005280 } },
	{ 363.0, { 0.175320, 0.005270, 0.819410 }, { 0.175320, 0.005270 } },
	{ 364.0, { 0.175240, 0.005260, 0.819500 }, { 0.175240, 0.005260 } },
	{ 365.0, { 0.175160, 0.005260, 0.819580 }, { 0.175160, 0.005260 } },
	{ 366.0, { 0.175090, 0.005250, 0.819660 }, { 0.175090, 0.005250 } },
	{ 367.0, { 0.175010, 0.005240, 0.819750 }, { 0.175010, 0.005240 } },
	{ 368.0, { 0.174940, 0.005230, 0.819830 }, { 0.174940, 0.005230 } },
	{ 369.0, { 0.174880, 0.005220, 0.819900 }, { 0.174880, 0.005220 } },
	{ 370.0, { 0.174820, 0.005220, 0.819960 }, { 0.174820, 0.005220 } },
	{ 371.0, { 0.174770, 0.005230, 0.820000 }, { 0.174770, 0.005230 } },
	{ 372.0, { 0.174720, 0.005240, 0.820040 }, { 0.174720, 0.005240 } },
	{ 373.0, { 0.174660, 0.005240, 0.820100 }, { 0.174660, 0.005240 } },
	{ 374.0, { 0.174590, 0.005220, 0.820190 }, { 0.174590, 0.005220 } },
	{ 375.0, { 0.174510, 0.005180, 0.820310 }, { 0.174510, 0.005180 } },
	{ 376.0, { 0.174410, 0.005130, 0.820460 }, { 0.174410, 0.005130 } },
	{ 377.0, { 0.174310, 0.005070, 0.820620 }, { 0.174310, 0.005070 } },
	{ 378.0, { 0.174220, 0.005020, 0.820760 }, { 0.174220, 0.005020 } },
	{ 379.0, { 0.174160, 0.004980, 0.820860 }, { 0.174160, 0.004980 } },
	{ 380.0, { 0.174110, 0.004960, 0.820930 }, { 0.174110, 0.004960 } },
	{ 381.0, { 0.174090, 0.004960, 0.820950 }, { 0.174090, 0.004960 } },
	{ 382.0, { 0.174070, 0.004970, 0.820960 }, { 0.174070, 0.004970 } },
	{ 383.0, { 0.174060, 0.004980, 0.820960 }, { 0.174060, 0.004980 } },
	{ 384.0, { 0.174040, 0.004980, 0.820980 }, { 0.174040, 0.004980 } },
	{ 385.0, { 0.174010, 0.004980, 0.821010 }, { 0.174010, 0.004980 } },
	{ 386.0, { 0.173970, 0.004970, 0.821060 }, { 0.173970, 0.004970 } },
	{ 387.0, { 0.173930, 0.004940, 0.821130 }, { 0.173930, 0.004940 } },
	{ 388.0, { 0.173890, 0.004930, 0.821180 }, { 0.173890, 0.004930 } },
	{ 389.0, { 0.173840, 0.004920, 0.821240 }, { 0.173840, 0.004920 } },
	{ 390.0, { 0.173800, 0.004920, 0.821280 }, { 0.173800, 0.004920 } },
	{ 391.0, { 0.173760, 0.004920, 0.821320 }, { 0.173760, 0.004920 } },
	{ 392.0, { 0.173700, 0.004940, 0.821360 }, { 0.173700, 0.004940 } },
	{ 393.0, { 0.173660, 0.004940, 0.821400 }, { 0.173660, 0.004940 } },
	{ 394.0, { 0.173610, 0.004940, 0.821450 }, { 0.173610, 0.004940 } },
	{ 395.0, { 0.173560, 0.004920, 0.821520 }, { 0.173560, 0.004920 } },
	{ 396.0, { 0.173510, 0.004900, 0.821590 }, { 0.173510, 0.004900 } },
	{ 397.0, { 0.173470, 0.004860, 0.821670 }, { 0.173470, 0.004860 } },
	{ 398.0, { 0.173420, 0.004840, 0.821740 }, { 0.173420, 0.004840 } },
	{ 399.0, { 0.173380, 0.004810, 0.821810 }, { 0.173380, 0.004810 } },
	{ 400.0, { 0.173340, 0.004800, 0.821860 }, { 0.173340, 0.004800 } },
	{ 401.0, { 0.173290, 0.004790, 0.821920 }, { 0.173290, 0.004790 } },
	{ 402.0, { 0.173240, 0.004780, 0.821980 }, { 0.173240, 0.004780 } },
	{ 403.0, { 0.173170, 0.004780, 0.822050 }, { 0.173170, 0.004780 } },
	{ 404.0, { 0.173100, 0.004770, 0.822130 }, { 0.173100, 0.004770 } },
	{ 405.0, { 0.173020, 0.004780, 0.822200 }, { 0.173020, 0.004780 } },
	{ 406.0, { 0.172930, 0.004780, 0.822290 }, { 0.172930, 0.004780 } },
	{ 407.0, { 0.172840, 0.004790, 0.822370 }, { 0.172840, 0.004790 } },
	{ 408.0, { 0.172750, 0.004800, 0.822450 }, { 0.172750, 0.004800 } },
	{ 409.0, { 0.172660, 0.004800, 0.822540 }, { 0.172660, 0.004800 } },
	{ 410.0, { 0.172580, 0.004800, 0.822620 }, { 0.172580, 0.004800 } },
	{ 411.0, { 0.172490, 0.004800, 0.822710 }, { 0.172490, 0.004800 } },
	{ 412.0, { 0.172390, 0.004800, 0.822810 }, { 0.172390, 0.004800 } },
	{ 413.0, { 0.172300, 0.004800, 0.822900 }, { 0.172300, 0.004800 } },
	{ 414.0, { 0.172190, 0.004820, 0.822990 }, { 0.172190, 0.004820 } },
	{ 415.0, { 0.172090, 0.004830, 0.823080 }, { 0.172090, 0.004830 } },
	{ 416.0, { 0.171980, 0.004860, 0.823160 }, { 0.171980, 0.004860 } },
	{ 417.0, { 0.171870, 0.004890, 0.823240 }, { 0.171870, 0.004890 } },
	{ 418.0, { 0.171740, 0.004940, 0.823320 }, { 0.171740, 0.004940 } },
	{ 419.0, { 0.171590, 0.005010, 0.823400 }, { 0.171590, 0.005010 } },
	{ 420.0, { 0.171410, 0.005100, 0.823490 }, { 0.171410, 0.005100 } },
	{ 421.0, { 0.171210, 0.005210, 0.823580 }, { 0.171210, 0.005210 } },
	{ 422.0, { 0.170990, 0.005330, 0.823680 }, { 0.170990, 0.005330 } },
	{ 423.0, { 0.170770, 0.005470, 0.823760 }, { 0.170770, 0.005470 } },
	{ 424.0, { 0.170540, 0.005620, 0.823840 }, { 0.170540, 0.005620 } },
	{ 425.0, { 0.170300, 0.005790, 0.823910 }, { 0.170300, 0.005790 } },
	{ 426.0, { 0.170050, 0.005970, 0.823980 }, { 0.170050, 0.005970 } },
	{ 427.0, { 0.169780, 0.006180, 0.824040 }, { 0.169780, 0.006180 } },
	{ 428.0, { 0.169500, 0.006400, 0.824100 }, { 0.169500, 0.006400 } },
	{ 429.0, { 0.169200, 0.006640, 0.824160 }, { 0.169200, 0.006640 } },
	{ 430.0, { 0.168880, 0.006900, 0.824220 }, { 0.168880, 0.006900 } },
	{ 431.0, { 0.168530, 0.007180, 0.824290 }, { 0.168530, 0.007180 } },
	{ 432.0, { 0.168150, 0.007490, 0.824360 }, { 0.168150, 0.007490 } },
	{ 433.0, { 0.167750, 0.007820, 0.824430 }, { 0.167750, 0.007820 } },
	{ 434.0, { 0.167330, 0.008170, 0.824500 }, { 0.167330, 0.008170 } },
	{ 435.0, { 0.166900, 0.008550, 0.824550 }, { 0.166900, 0.008550 } },
	{ 436.0, { 0.166450, 0.008960, 0.824590 }, { 0.166450, 0.008960 } },
	{ 437.0, { 0.165980, 0.009400, 0.824620 }, { 0.165980, 0.009400 } },
	{ 438.0, { 0.165480, 0.009870, 0.824650 }, { 0.165480, 0.009870 } },
	{ 439.0, { 0.164960, 0.010350, 0.824690 }, { 0.164960, 0.010350 } },
	{ 440.0, { 0.164410, 0.010860, 0.824730 }, { 0.164410, 0.010860 } },
	{ 441.0, { 0.163830, 0.011380, 0.824790 }, { 0.163830, 0.011380 } },
	{ 442.0, { 0.163210, 0.011940, 0.824850 }, { 0.163210, 0.011940 } },
	{ 443.0, { 0.162550, 0.012520, 0.824930 }, { 0.162550, 0.012520 } },
	{ 444.0, { 0.161850, 0.013140, 0.825010 }, { 0.161850, 0.013140 } },
	{ 445.0, { 0.161110, 0.013790, 0.825100 }, { 0.161110, 0.013790 } },
	{ 446.0, { 0.160310, 0.014490, 0.825200 }, { 0.160310, 0.014490 } },
	{ 447.0, { 0.159470, 0.015230, 0.825300 }, { 0.159470, 0.015230 } },
	{ 448.0, { 0.158570, 0.016020, 0.825410 }, { 0.158570, 0.016020 } },
	{ 449.0, { 0.157630, 0.016840, 0.825530 }, { 0.157630, 0.016840 } },
	{ 450.0, { 0.156640, 0.017710, 0.825650 }, { 0.156640, 0.017710 } },
	{ 451.0, { 0.155600, 0.018610, 0.825790 }, { 0.155600, 0.018610 } },
	{ 452.0, { 0.154520, 0.019560, 0.825920 }, { 0.154520, 0.019560 } },
	{ 453.0, { 0.153400, 0.020550, 0.826050 }, { 0.153400, 0.020550 } },
	{ 454.0, { 0.152220, 0.021610, 0.826170 }, { 0.152220, 0.021610 } },
	{ 455.0, { 0.150990, 0.022740, 0.826270 }, { 0.150990, 0.022740 } },
	{ 456.0, { 0.149690, 0.023950, 0.826360 }, { 0.149690, 0.023950 } },
	{ 457.0, { 0.148340, 0.025250, 0.826410 }, { 0.148340, 0.025250 } },
	{ 458.0, { 0.146930, 0.026630, 0.826440 }, { 0.146930, 0.026630 } },
	{ 459.0, { 0.145470, 0.028120, 0.826410 }, { 0.145470, 0.028120 } },
	{ 460.0, { 0.143960, 0.029700, 0.826340 }, { 0.143960, 0.029700 } },
	{ 461.0, { 0.142410, 0.031390, 0.826200 }, { 0.142410, 0.031390 } },
	{ 462.0, { 0.140800, 0.033210, 0.825990 }, { 0.140800, 0.033210 } },
	{ 463.0, { 0.139120, 0.035200, 0.825680 }, { 0.139120, 0.035200 } },
	{ 464.0, { 0.137370, 0.037400, 0.825230 }, { 0.137370, 0.037400 } },
	{ 465.0, { 0.135500, 0.039880, 0.824620 }, { 0.135500, 0.039880 } },
	{ 466.0, { 0.133510, 0.042690, 0.823800 }, { 0.133510, 0.042690 } },
	{ 467.0, { 0.131370, 0.045880, 0.822750 }, { 0.131370, 0.045880 } },
	{ 468.0, { 0.129090, 0.049450, 0.821460 }, { 0.129090, 0.049450 } },
	{ 469.0, { 0.126660, 0.053430, 0.819910 }, { 0.126660, 0.053430 } },
	{ 470.0, { 0.124120, 0.057800, 0.818080 }, { 0.124120, 0.057800 } },
	{ 471.0, { 0.121470, 0.062590, 0.815940 }, { 0.121470, 0.062590 } },
	{ 472.0, { 0.118700, 0.067830, 0.813470 }, { 0.118700, 0.067830 } },
	{ 473.0, { 0.115810, 0.073580, 0.810610 }, { 0.115810, 0.073580 } },
	{ 474.0, { 0.112780, 0.079890, 0.807330 }, { 0.112780, 0.079890 } },
	{ 475.0, { 0.109600, 0.086840, 0.803560 }, { 0.109600, 0.086840 } },
	{ 476.0, { 0.106260, 0.094490, 0.799250 }, { 0.106260, 0.094490 } },
	{ 477.0, { 0.102780, 0.102860, 0.794360 }, { 0.102780, 0.102860 } },
	{ 478.0, { 0.099130, 0.112010, 0.788860 }, { 0.099130, 0.112010 } },
	{ 479.0, { 0.095310, 0.121940, 0.782750 }, { 0.095310, 0.121940 } },
	{ 480.0, { 0.091290, 0.132700, 0.776010 }, { 0.091290, 0.132700 } },
	{ 481.0, { 0.087080, 0.144320, 0.768600 }, { 0.087080, 0.144320 } },
	{ 482.0, { 0.082680, 0.156870, 0.760450 }, { 0.082680, 0.156870 } },
	{ 483.0, { 0.078120, 0.170420, 0.751460 }, { 0.078120, 0.170420 } },
	{ 484.0, { 0.073440, 0.185030, 0.741530 }, { 0.073440, 0.185030 } },
	{ 485.0, { 0.068710, 0.200720, 0.730570 }, { 0.068710, 0.200720 } },
	{ 486.0, { 0.063990, 0.217470, 0.718540 }, { 0.063990, 0.217470 } },
	{ 487.0, { 0.059320, 0.235250, 0.705430 }, { 0.059320, 0.235250 } },
	{ 488.0, { 0.054670, 0.254090, 0.691240 }, { 0.054670, 0.254090 } },
	{ 489.0, { 0.050030, 0.274000, 0.675970 }, { 0.050030, 0.274000 } },
	{ 490.0, { 0.045390, 0.294980, 0.659630 }, { 0.045390, 0.294980 } },
	{ 491.0, { 0.040760, 0.316980, 0.642260 }, { 0.040760, 0.316980 } },
	{ 492.0, { 0.036200, 0.339900, 0.623900 }, { 0.036200, 0.339900 } },
	{ 493.0, { 0.031760, 0.363600, 0.604640 }, { 0.031760, 0.363600 } },
	{ 494.0, { 0.027490, 0.387920, 0.584590 }, { 0.027490, 0.387920 } },
	{ 495.0, { 0.023460, 0.412700, 0.563840 }, { 0.023460, 0.412700 } },
	{ 496.0, { 0.019700, 0.437760, 0.542540 }, { 0.019700, 0.437760 } },
	{ 497.0, { 0.016270, 0.462950, 0.520780 }, { 0.016270, 0.462950 } },
	{ 498.0, { 0.013180, 0.488210, 0.498610 }, { 0.013180, 0.488210 } },
	{ 499.0, { 0.010480, 0.513400, 0.476120 }, { 0.010480, 0.513400 } },
	{ 500.0, { 0.008170, 0.538420, 0.453410 }, { 0.008170, 0.538420 } },
	{ 501.0, { 0.006280, 0.563070, 0.430650 }, { 0.006280, 0.563070 } },
	{ 502.0, { 0.004870, 0.587120, 0.408010 }, { 0.004870, 0.587120 } },
	{ 503.0, { 0.003980, 0.610450, 0.385570 }, { 0.003980, 0.610450 } },
	{ 504.0, { 0.003640, 0.633010, 0.363350 }, { 0.003640, 0.633010 } },
	{ 505.0, { 0.003860, 0.654820, 0.341320 }, { 0.003860, 0.654820 } },
	{ 506.0, { 0.004640, 0.675900, 0.319460 }, { 0.004640, 0.675900 } },
	{ 507.0, { 0.006010, 0.696120, 0.297870 }, { 0.006010, 0.696120 } },
	{ 508.0, { 0.007990, 0.715340, 0.276670 }, { 0.007990, 0.715340 } },
	{ 509.0, { 0.010600, 0.733410, 0.255990 }, { 0.010600, 0.733410 } },
	{ 510.0, { 0.013870, 0.750190, 0.235940 }, { 0.013870, 0.750190 } },
	{ 511.0, { 0.017770, 0.765610, 0.216620 }, { 0.017770, 0.765610 } },
	{ 512.0, { 0.022240, 0.779630, 0.198130 }, { 0.022240, 0.779630 } },
	{ 513.0, { 0.027270, 0.792110, 0.180620 }, { 0.027270, 0.792110 } },
	{ 514.0, { 0.032820, 0.802930, 0.164250 }, { 0.032820, 0.802930 } },
	{ 515.0, { 0.038850, 0.812020, 0.149130 }, { 0.038850, 0.812020 } },
	{ 516.0, { 0.045330, 0.819390, 0.135280 }, { 0.045330, 0.819390 } },
	{ 517.0, { 0.052180, 0.825160, 0.122660 }, { 0.052180, 0.825160 } },
	{ 518.0, { 0.059320, 0.829430, 0.111250 }, { 0.059320, 0.829430 } },
	{ 519.0, { 0.066720, 0.832270, 0.101010 }, { 0.066720, 0.832270 } },
	{ 520.0, { 0.074300, 0.833800, 0.091900 }, { 0.074300, 0.833800 } },
	{ 521.0, { 0.082050, 0.834090, 0.083860 }, { 0.082050, 0.834090 } },
	{ 522.0, { 0.089940, 0.833290, 0.076770 }, { 0.089940, 0.833290 } },
	{ 523.0, { 0.097940, 0.831590, 0.070470 }, { 0.097940, 0.831590 } },
	{ 524.0, { 0.106020, 0.829180, 0.064800 }, { 0.106020, 0.829180 } },
	{ 525.0, { 0.114160, 0.826210, 0.059630 }, { 0.114160, 0.826210 } },
	{ 526.0, { 0.122350, 0.822770, 0.054880 }, { 0.122350, 0.822770 } },
	{ 527.0, { 0.130550, 0.818930, 0.050520 }, { 0.130550, 0.818930 } },
	{ 528.0, { 0.138700, 0.814780, 0.046520 }, { 0.138700, 0.814780 } },
	{ 529.0, { 0.146770, 0.810400, 0.042830 }, { 0.146770, 0.810400 } },
	{ 530.0, { 0.154720, 0.805860, 0.039420 }, { 0.154720, 0.805860 } },
	{ 531.0, { 0.162530, 0.801240, 0.036230 }, { 0.162530, 0.801240 } },
	{ 532.0, { 0.170240, 0.796520, 0.033240 }, { 0.170240, 0.796520 } },
	{ 533.0, { 0.177850, 0.791690, 0.030460 }, { 0.177850, 0.791690 } },
	{ 534.0, { 0.185390, 0.786730, 0.027880 }, { 0.185390, 0.786730 } },
	{ 535.0, { 0.192880, 0.781630, 0.025490 }, { 0.192880, 0.781630 } },
	{ 536.0, { 0.200310, 0.776400, 0.023290 }, { 0.200310, 0.776400 } },
	{ 537.0, { 0.207690, 0.771050, 0.021260 }, { 0.207690, 0.771050 } },
	{ 538.0, { 0.215030, 0.765590, 0.019380 }, { 0.215030, 0.765590 } },
	{ 539.0, { 0.222340, 0.760020, 0.017640 }, { 0.222340, 0.760020 } },
	{ 540.0, { 0.229620, 0.754330, 0.016050 }, { 0.229620, 0.754330 } },
	{ 541.0, { 0.236890, 0.748520, 0.014590 }, { 0.236890, 0.748520 } },
	{ 542.0, { 0.244130, 0.742620, 0.013250 }, { 0.244130, 0.742620 } },
	{ 543.0, { 0.251360, 0.736610, 0.012030 }, { 0.251360, 0.736610 } },
	{ 544.0, { 0.258580, 0.730510, 0.010910 }, { 0.258580, 0.730510 } },
	{ 545.0, { 0.265780, 0.724320, 0.009900 }, { 0.265780, 0.724320 } },
	{ 546.0, { 0.272960, 0.718060, 0.008980 }, { 0.272960, 0.718060 } },
	{ 547.0, { 0.280130, 0.711720, 0.008150 }, { 0.280130, 0.711720 } },
	{ 548.0, { 0.287290, 0.705320, 0.007390 }, { 0.287290, 0.705320 } },
	{ 549.0, { 0.294450, 0.698840, 0.006710 }, { 0.294450, 0.698840 } },
	{ 550.0, { 0.301600, 0.692310, 0.006090 }, { 0.301600, 0.692310 } },
	{ 551.0, { 0.308760, 0.685710, 0.005530 }, { 0.308760, 0.685710 } },
	{ 552.0, { 0.315920, 0.679060, 0.005020 }, { 0.315920, 0.679060 } },
	{ 553.0, { 0.323060, 0.672370, 0.004570 }, { 0.323060, 0.672370 } },
	{ 554.0, { 0.330210, 0.665630, 0.004160 }, { 0.330210, 0.665630 } },
	{ 555.0, { 0.337360, 0.658850, 0.003790 }, { 0.337360, 0.658850 } },
	{ 556.0, { 0.344510, 0.652030, 0.003460 }, { 0.344510, 0.652030 } },
	{ 557.0, { 0.351670, 0.645170, 0.003160 }, { 0.351670, 0.645170 } },
	{ 558.0, { 0.358810, 0.638290, 0.002900 }, { 0.358810, 0.638290 } },
	{ 559.0, { 0.365960, 0.631380, 0.002660 }, { 0.365960, 0.631380 } },
	{ 560.0, { 0.373100, 0.624450, 0.002450 }, { 0.373100, 0.624450 } },
	{ 561.0, { 0.380240, 0.617500, 0.002260 }, { 0.380240, 0.617500 } },
	{ 562.0, { 0.387380, 0.610540, 0.002080 }, { 0.387380, 0.610540 } },
	{ 563.0, { 0.394510, 0.603570, 0.001920 }, { 0.394510, 0.603570 } },
	{ 564.0, { 0.401630, 0.596590, 0.001780 }, { 0.401630, 0.596590 } },
	{ 565.0, { 0.408730, 0.589610, 0.001660 }, { 0.408730, 0.589610 } },
	{ 566.0, { 0.415830, 0.582620, 0.001550 }, { 0.415830, 0.582620 } },
	{ 567.0, { 0.422920, 0.575630, 0.001450 }, { 0.422920, 0.575630 } },
	{ 568.0, { 0.429990, 0.568650, 0.001360 }, { 0.429990, 0.568650 } },
	{ 569.0, { 0.437040, 0.561670, 0.001290 }, { 0.437040, 0.561670 } },
	{ 570.0, { 0.444060, 0.554720, 0.001220 }, { 0.444060, 0.554720 } },
	{ 571.0, { 0.451060, 0.547770, 0.001170 }, { 0.451060, 0.547770 } },
	{ 572.0, { 0.458040, 0.540840, 0.001120 }, { 0.458040, 0.540840 } },
	{ 573.0, { 0.464990, 0.533930, 0.001080 }, { 0.464990, 0.533930 } },
	{ 574.0, { 0.471900, 0.527050, 0.001050 }, { 0.471900, 0.527050 } },
	{ 575.0, { 0.478780, 0.520200, 0.001020 }, { 0.478780, 0.520200 } },
	{ 576.0, { 0.485610, 0.513390, 0.001000 }, { 0.485610, 0.513390 } },
	{ 577.0, { 0.492410, 0.506610, 0.000980 }, { 0.492410, 0.506610 } },
	{ 578.0, { 0.499150, 0.499890, 0.000960 }, { 0.499150, 0.499890 } },
	{ 579.0, { 0.505850, 0.493210, 0.000940 }, { 0.505850, 0.493210 } },
	{ 580.0, { 0.512490, 0.486590, 0.000920 }, { 0.512490, 0.486590 } },
	{ 581.0, { 0.519070, 0.480030, 0.000900 }, { 0.519070, 0.480030 } },
	{ 582.0, { 0.525600, 0.473530, 0.000870 }, { 0.525600, 0.473530 } },
	{ 583.0, { 0.532070, 0.467090, 0.000840 }, { 0.532070, 0.467090 } },
	{ 584.0, { 0.538460, 0.460730, 0.000810 }, { 0.538460, 0.460730 } },
	{ 585.0, { 0.544790, 0.454430, 0.000780 }, { 0.544790, 0.454430 } },
	{ 586.0, { 0.551030, 0.448230, 0.000740 }, { 0.551030, 0.448230 } },
	{ 587.0, { 0.557190, 0.442100, 0.000710 }, { 0.557190, 0.442100 } },
	{ 588.0, { 0.563270, 0.436060, 0.000670 }, { 0.563270, 0.436060 } },
	{ 589.0, { 0.569260, 0.430100, 0.000640 }, { 0.569260, 0.430100 } },
	{ 590.0, { 0.575150, 0.424230, 0.000620 }, { 0.575150, 0.424230 } },
	{ 591.0, { 0.580940, 0.418460, 0.000600 }, { 0.580940, 0.418460 } },
	{ 592.0, { 0.586650, 0.412760, 0.000590 }, { 0.586650, 0.412760 } },
	{ 593.0, { 0.592220, 0.407190, 0.000590 }, { 0.592220, 0.407190 } },
	{ 594.0, { 0.597660, 0.401760, 0.000580 }, { 0.597660, 0.401760 } },
	{ 595.0, { 0.602930, 0.396500, 0.000570 }, { 0.602930, 0.396500 } },
	{ 596.0, { 0.608030, 0.391410, 0.000560 }, { 0.608030, 0.391410 } },
	{ 597.0, { 0.612980, 0.386480, 0.000540 }, { 0.612980, 0.386480 } },
	{ 598.0, { 0.617780, 0.381710, 0.000510 }, { 0.617780, 0.381710 } },
	{ 599.0, { 0.622460, 0.377050, 0.000490 }, { 0.622460, 0.377050 } },
	{ 600.0, { 0.627040, 0.372490, 0.000470 }, { 0.627040, 0.372490 } },
	{ 601.0, { 0.631520, 0.368030, 0.000450 }, { 0.631520, 0.368030 } },
	{ 602.0, { 0.635900, 0.363670, 0.000430 }, { 0.635900, 0.363670 } },
	{ 603.0, { 0.640160, 0.359430, 0.000410 }, { 0.640160, 0.359430 } },
	{ 604.0, { 0.644270, 0.355330, 0.000400 }, { 0.644270, 0.355330 } },
	{ 605.0, { 0.648230, 0.351400, 0.000370 }, { 0.648230, 0.351400 } },
	{ 606.0, { 0.652030, 0.347630, 0.000340 }, { 0.652030, 0.347630 } },
	{ 607.0, { 0.655670, 0.344020, 0.000310 }, { 0.655670, 0.344020 } },
	{ 608.0, { 0.659170, 0.340550, 0.000280 }, { 0.659170, 0.340550 } },
	{ 609.0, { 0.662530, 0.337220, 0.000250 }, { 0.662530, 0.337220 } },
	{ 610.0, { 0.665760, 0.334010, 0.000230 }, { 0.665760, 0.334010 } },
	{ 611.0, { 0.668870, 0.330920, 0.000210 }, { 0.668870, 0.330920 } },
	{ 612.0, { 0.671860, 0.327950, 0.000190 }, { 0.671860, 0.327950 } },
	{ 613.0, { 0.674720, 0.325090, 0.000190 }, { 0.674720, 0.325090 } },
	{ 614.0, { 0.677460, 0.322360, 0.000180 }, { 0.677460, 0.322360 } },
	{ 615.0, { 0.680080, 0.319750, 0.000170 }, { 0.680080, 0.319750 } },
	{ 616.0, { 0.682580, 0.317250, 0.000170 }, { 0.682580, 0.317250 } },
	{ 617.0, { 0.684970, 0.314860, 0.000170 }, { 0.684970, 0.314860 } },
	{ 618.0, { 0.687250, 0.312590, 0.000160 }, { 0.687250, 0.312590 } },
	{ 619.0, { 0.689430, 0.310410, 0.000160 }, { 0.689430, 0.310410 } },
	{ 620.0, { 0.691510, 0.308340, 0.000150 }, { 0.691510, 0.308340 } },
	{ 621.0, { 0.693490, 0.306370, 0.000140 }, { 0.693490, 0.306370 } },
	{ 622.0, { 0.695390, 0.304480, 0.000130 }, { 0.695390, 0.304480 } },
	{ 623.0, { 0.697210, 0.302670, 0.000120 }, { 0.697210, 0.302670 } },
	{ 624.0, { 0.698940, 0.300950, 0.000110 }, { 0.698940, 0.300950 } },
	{ 625.0, { 0.700610, 0.299300, 0.000090 }, { 0.700610, 0.299300 } },
	{ 626.0, { 0.702190, 0.297730, 0.000080 }, { 0.702190, 0.297730 } },
	{ 627.0, { 0.703710, 0.296220, 0.000070 }, { 0.703710, 0.296220 } },
	{ 628.0, { 0.705160, 0.294770, 0.000070 }, { 0.705160, 0.294770 } },
	{ 629.0, { 0.706560, 0.293380, 0.000060 }, { 0.706560, 0.293380 } },
	{ 630.0, { 0.707920, 0.292030, 0.000050 }, { 0.707920, 0.292030 } },
	{ 631.0, { 0.709230, 0.290720, 0.000050 }, { 0.709230, 0.290720 } },
	{ 632.0, { 0.710500, 0.289450, 0.000050 }, { 0.710500, 0.289450 } },
	{ 633.0, { 0.711730, 0.288230, 0.000040 }, { 0.711730, 0.288230 } },
	{ 634.0, { 0.712900, 0.287060, 0.000040 }, { 0.712900, 0.287060 } },
	{ 635.0, { 0.714030, 0.285930, 0.000040 }, { 0.714030, 0.285930 } },
	{ 636.0, { 0.715120, 0.284840, 0.000040 }, { 0.715120, 0.284840 } },
	{ 637.0, { 0.716160, 0.283800, 0.000040 }, { 0.716160, 0.283800 } },
	{ 638.0, { 0.717160, 0.282810, 0.000030 }, { 0.717160, 0.282810 } },
	{ 639.0, { 0.718120, 0.281850, 0.000030 }, { 0.718120, 0.281850 } },
	{ 640.0, { 0.719030, 0.280940, 0.000030 }, { 0.719030, 0.280940 } },
	{ 641.0, { 0.719910, 0.280060, 0.000030 }, { 0.719910, 0.280060 } },
	{ 642.0, { 0.720750, 0.279220, 0.000030 }, { 0.720750, 0.279220 } },
	{ 643.0, { 0.721550, 0.278420, 0.000030 }, { 0.721550, 0.278420 } },
	{ 644.0, { 0.722320, 0.277660, 0.000020 }, { 0.722320, 0.277660 } },
	{ 645.0, { 0.723030, 0.276950, 0.000020 }, { 0.723030, 0.276950 } },
	{ 646.0, { 0.723700, 0.276280, 0.000020 }, { 0.723700, 0.276280 } },
	{ 647.0, { 0.724330, 0.275660, 0.000010 }, { 0.724330, 0.275660 } },
	{ 648.0, { 0.724910, 0.275080, 0.000010 }, { 0.724910, 0.275080 } },
	{ 649.0, { 0.725470, 0.274530, 0.000000 }, { 0.725470, 0.274530 } },
	{ 650.0, { 0.725990, 0.274010, 0.000000 }, { 0.725990, 0.274010 } },
	{ 651.0, { 0.726490, 0.273510, 0.000000 }, { 0.726490, 0.273510 } },
	{ 652.0, { 0.726980, 0.273020, 0.000000 }, { 0.726980, 0.273020 } },
	{ 653.0, { 0.727430, 0.272570, 0.000000 }, { 0.727430, 0.272570 } },
	{ 654.0, { 0.727860, 0.272140, 0.000000 }, { 0.727860, 0.272140 } },
	{ 655.0, { 0.728270, 0.271730, 0.000000 }, { 0.728270, 0.271730 } },
	{ 656.0, { 0.728660, 0.271340, 0.000000 }, { 0.728660, 0.271340 } },
	{ 657.0, { 0.729020, 0.270980, 0.000000 }, { 0.729020, 0.270980 } },
	{ 658.0, { 0.729360, 0.270640, 0.000000 }, { 0.729360, 0.270640 } },
	{ 659.0, { 0.729680, 0.270320, 0.000000 }, { 0.729680, 0.270320 } },
	{ 660.0, { 0.729970, 0.270030, 0.000000 }, { 0.729970, 0.270030 } },
	{ 661.0, { 0.730230, 0.269770, 0.000000 }, { 0.730230, 0.269770 } },
	{ 662.0, { 0.730470, 0.269530, 0.000000 }, { 0.730470, 0.269530 } },
	{ 663.0, { 0.730690, 0.269310, 0.000000 }, { 0.730690, 0.269310 } },
	{ 664.0, { 0.730900, 0.269100, 0.000000 }, { 0.730900, 0.269100 } },
	{ 665.0, { 0.731090, 0.268910, 0.000000 }, { 0.731090, 0.268910 } },
	{ 666.0, { 0.731280, 0.268720, 0.000000 }, { 0.731280, 0.268720 } },
	{ 667.0, { 0.731470, 0.268530, 0.000000 }, { 0.731470, 0.268530 } },
	{ 668.0, { 0.731650, 0.268350, 0.000000 }, { 0.731650, 0.268350 } },
	{ 669.0, { 0.731830, 0.268170, 0.000000 }, { 0.731830, 0.268170 } },
	{ 670.0, { 0.731990, 0.268010, 0.000000 }, { 0.731990, 0.268010 } },
	{ 671.0, { 0.732150, 0.267850, 0.000000 }, { 0.732150, 0.267850 } },
	{ 672.0, { 0.732300, 0.267700, 0.000000 }, { 0.732300, 0.267700 } },
	{ 673.0, { 0.732440, 0.267560, 0.000000 }, { 0.732440, 0.267560 } },
	{ 674.0, { 0.732580, 0.267420, 0.000000 }, { 0.732580, 0.267420 } },
	{ 675.0, { 0.732720, 0.267280, 0.000000 }, { 0.732720, 0.267280 } },
	{ 676.0, { 0.732860, 0.267140, 0.000000 }, { 0.732860, 0.267140 } },
	{ 677.0, { 0.733000, 0.267000, 0.000000 }, { 0.733000, 0.267000 } },
	{ 678.0, { 0.733140, 0.266860, 0.000000 }, { 0.733140, 0.266860 } },
	{ 679.0, { 0.733280, 0.266720, 0.000000 }, { 0.733280, 0.266720 } },
	{ 680.0, { 0.733420, 0.266580, 0.000000 }, { 0.733420, 0.266580 } },
	{ 681.0, { 0.733550, 0.266450, 0.000000 }, { 0.733550, 0.266450 } },
	{ 682.0, { 0.733680, 0.266320, 0.000000 }, { 0.733680, 0.266320 } },
	{ 683.0, { 0.733810, 0.266190, 0.000000 }, { 0.733810, 0.266190 } },
	{ 684.0, { 0.733940, 0.266060, 0.000000 }, { 0.733940, 0.266060 } },
	{ 685.0, { 0.734050, 0.265950, 0.000000 }, { 0.734050, 0.265950 } },
	{ 686.0, { 0.734140, 0.265860, 0.000000 }, { 0.734140, 0.265860 } },
	{ 687.0, { 0.734220, 0.265780, 0.000000 }, { 0.734220, 0.265780 } },
	{ 688.0, { 0.734290, 0.265710, 0.000000 }, { 0.734290, 0.265710 } },
	{ 689.0, { 0.734340, 0.265660, 0.000000 }, { 0.734340, 0.265660 } },
	{ 690.0, { 0.734390, 0.265610, 0.000000 }, { 0.734390, 0.265610 } },
	{ 691.0, { 0.734440, 0.265560, 0.000000 }, { 0.734440, 0.265560 } },
	{ 692.0, { 0.734480, 0.265520, 0.000000 }, { 0.734480, 0.265520 } },
	{ 693.0, { 0.734520, 0.265480, 0.000000 }, { 0.734520, 0.265480 } },
	{ 694.0, { 0.734560, 0.265440, 0.000000 }, { 0.734560, 0.265440 } },
	{ 695.0, { 0.734590, 0.265410, 0.000000 }, { 0.734590, 0.265410 } },
	{ 696.0, { 0.734620, 0.265380, 0.000000 }, { 0.734620, 0.265380 } },
	{ 697.0, { 0.734650, 0.265350, 0.000000 }, { 0.734650, 0.265350 } },
	{ 698.0, { 0.734670, 0.265330, 0.000000 }, { 0.734670, 0.265330 } },
	{ 699.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 700.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 701.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 702.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 703.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 704.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 705.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 706.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 707.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 708.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 709.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 710.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 711.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 712.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 713.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 714.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 715.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 716.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 717.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 718.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 719.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 720.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 721.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 722.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 723.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 724.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 725.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 726.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 727.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 728.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 729.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 730.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 731.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 732.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 733.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 734.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 735.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 736.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 737.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 738.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 739.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 740.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 741.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 742.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 743.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 744.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 745.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 746.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 747.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 748.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 749.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 750.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 751.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 752.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 753.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 754.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 755.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 756.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 757.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 758.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 759.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 760.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 761.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 762.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 763.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 764.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 765.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 766.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 767.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 768.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 769.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 770.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 771.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 772.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 773.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 774.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 775.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 776.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 777.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 778.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 779.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 780.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 781.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 782.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 783.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 784.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 785.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 786.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 787.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 788.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 789.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 790.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 791.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 792.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 793.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 794.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 795.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 796.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 797.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 798.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 799.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 800.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 801.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 802.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 803.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 804.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 805.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 806.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 807.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 808.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 809.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 810.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 811.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 812.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 813.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 814.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 815.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 816.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 817.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 818.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 819.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 820.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 821.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 822.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 823.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 824.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 825.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 826.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 827.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 828.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 829.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
	{ 830.0, { 0.734690, 0.265310, 0.000000 }, { 0.734690, 0.265310 } },
};

} // namespace promeki
