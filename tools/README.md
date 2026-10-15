# tools/

Scripts de análisis en Python para investigar bloqueos sin abrir Ghidra. Leen **tu copia local** del juego (`game/disc/`); no contienen ni generan nada del juego que se suba al repo.

```powershell
py -3 -m pip install capstone pyelftools
```

| Script | Para qué |
|---|---|
| `elf_dis.py 0xINICIO 0xFIN` | Desensamblar un rango del ELF. `--str 0xDIR …` lee cadenas (nombres de recursos, mensajes de error). |
| `elf_xref.py "jal 0x13d710"` | Buscar quién llama a una función o quién accede a una global (`"-0x3b24("` con base `0x2A0000`). |
| `irx_dis.py game/disc/X.IRX 0xINICIO 0xFIN` | Desensamblar un módulo del IOP marcando sus imports (`sysmem:4`, `cdvdman:6`…). Direcciones relativas al módulo: súmales la `base=` que sale en el log al cargarlo. |
| `vu_dis.py volcado.bin 0xINICIO 0xFIN` | Desensamblar microcódigo de la VU1 a partir de un volcado (16 KB de código al principio): saltos, aritmética entera, cargas/escrituras y `XGKICK`, con los bits I/E/M/D/T. Sin dependencias. |
| `scan_missing_functions.py` | Buscar funciones que Ghidra no detecta e imprimirlas como `extra_entry_points`: métodos virtuales (`vfn_`), destinos de `JAL` en huecos (`fn_`) y punteros que el código construye con `lui`+`addiu` (`cfn_`, callbacks). **Ejecutadlo después de cada export de Ghidra:** si sale algo, pegadlo en `config/gow.manual.toml`. |

Capstone decodifica MIPS32 estándar: lo propio del Emotion Engine (`sq`/`lq` de 128 bits, MMI, `daddu`…) sale como `.byte`. Para eso, Ghidra con la extensión EE o los comentarios `// 0x…` de los archivos de `generated/`.
