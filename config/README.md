# config/

| Archivo | Qué es | Quién lo genera |
|---|---|---|
| `gow.toml` | Configuración del recompilador (stubs, bindings, parches) | Export de Ghidra, luego se edita a mano |
| `gow_functions.csv` | Mapa de funciones (nombre, inicio, fin) | Export de Ghidra |

Los dos **sí** se suben al repo: así todos recompilamos con los mismos límites de función.

- Exporta siempre a esta carpeta con estos nombres exactos.
- `scripts/recompile.ps1` reescribe `input`, `output` y `ghidra_output` a rutas relativas, así que no te preocupes por las rutas absolutas que pone Ghidra.
- Si vuelves a exportar desde Ghidra, se pierden los cambios hechos a mano en `gow.toml`. Antes de exportar, guarda tus `stubs` y `patches` y vuelve a añadirlos después (revísalo con `git diff`).
