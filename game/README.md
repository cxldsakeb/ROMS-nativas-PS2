# game/ (solo local)

Aquí vive **tu** copia del juego. Git ignora todo lo que hay en esta carpeta salvo este archivo.

Se rellena con:

```powershell
.\scripts\extract-iso.ps1 -IsoPath "ruta\a\God of War.iso"
```

Resultado:

```
game/
  disc/            ← contenido completo del DVD (el runtime lo usa como cdrom0:)
    SCUS_973.99    ← ELF principal (el nombre depende de la región)
    ...
  elf-name.txt     ← nombre del ELF detectado (lo leen los scripts)
```

**Nunca** subas nada de aquí al repo ni lo compartas por Discord u otros sitios. Cada uno usa su propia copia del juego.
