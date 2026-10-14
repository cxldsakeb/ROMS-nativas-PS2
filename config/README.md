# config/

| Archivo | Qué es | Quién lo genera | ¿Se edita a mano? |
|---|---|---|---|
| `gow.toml` | Config base del recompilador | `scripts/ghidra-analyze.ps1` (export de Ghidra) | **No**: se sobrescribe en cada export |
| `gow_functions.csv` | Mapa de funciones (nombre, inicio, fin) | `scripts/ghidra-analyze.ps1` | No |
| `gow.manual.toml` | **Añadidos del equipo**: stubs, entry points, MMIO, parches | Nosotros | **Sí, aquí van todos los cambios** |

`scripts/recompile.ps1` los combina en `out/gow.effective.toml` y recompila con ese archivo:

- `extra_stubs` se añade a `stubs` y `extra_entry_points` a `entry_points` (en `[general]`).
- El resto de secciones de `gow.manual.toml` (`[mmio]`, `[patches]`…) se añade tal cual al final.

Así se puede volver a exportar desde Ghidra (por ejemplo, después de corregir límites de función en la interfaz) sin perder nada.

Formato de las entradas:

```toml
extra_stubs = [
  "sceCdRead@0x00123456",   # [quién] binding a un handler del runtime
  "ret0@0x001D9410",        # [quién] TEMPORAL: por qué
]
extra_entry_points = [
  "mi_funcion@0x0029BB78",  # [quién] función que Ghidra no detecta
]
```
