# capmetr
Repozitář obsahuje firmware přípravku pro měření napěťové závislosti kapacity keramických kondenzátorů. Obsahuje kompletní projekt pro STM32CubeIDE určený pro vývojovou desku NUCLEO-F446RE. 

Program zajišťuje generování měřicího sinusového signálu pomocí DAC, snímání napětí pomocí ADC s použitím DMA a následný výpočet kapacity měřeného kondenzátoru.

Po připojení kondenzátoru se automaticky zvolí vhodný měřicí rozsah (1-10 uF nebo 10-100 uF). Na základě naměřené hodnoty napětí na kapacitním děliči s měřeným kondenzátorem je vypočítána jeho kapacita. Hodnoty kapacity a stejnosměrného předpětí jsou ukládány do kruhové fronty, zobrazovány na LCD displeji a pomocí tlačítek lze procházet hodnoty z předchozích měření.

Součástí měřicího cyklu je dále vybíjení kondenzátoru kapacitního děliče. Frekvence sinusového signálu je specifická pro jednotlivé měřicí rozsahy, generuje sinusový signál o frekvenci 1 kHz pro rozsah 1-10 uF nebo 120 Hz pro rozsah 10-100 uF.

Součástí práce byl dále návrh PCB a mechanická konstrukce měřicího přípravku. 
