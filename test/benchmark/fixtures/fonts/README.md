# Private test fonts

`KaushanScript-Regular.otf` is the unmodified Kaushan Script 1.002 OpenType CFF
font by Pablo Impallari and Igino Marini. It is distributed under the included
SIL Open Font License 1.1; its embedded name table also declares that license.
The family license is maintained at
[Google Fonts](https://github.com/google/fonts/blob/main/ofl/kaushanscript/OFL.txt).

SHA-256: `c007ebe4c813374ed02da003394b73b39fa709ff5891c5613733c96be595ba28`.

This font reproduces GDI font-realization failure at the native renderer's
large intermediate sizes. Tests load it privately and never install it.
TrueType coverage uses the Windows Arial font. The subtitle fixture contains
synthetic text, with no video or dialogue from the submitted samples.
