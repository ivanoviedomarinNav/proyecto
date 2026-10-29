# Carpetas seguras

Dos programas nativos de Windows (C++) para cifrar y descifrar tres carpetas del
Escritorio:

- `documentos importantes`
- `proyectos personales`
- `datos privados`

No mueven nada: las carpetas se quedan donde están y los archivos se conservan
con el mismo nombre, pero su contenido pasa a ser binario ilegible. Al abrirlos
con el Bloc de notas solo se ven símbolos raros.

---

## Lo que tienes que usar

| Archivo | Para qué sirve |
|---|---|
| `dist\Cifrar.exe` | Un clic y un botón. Cifra las tres carpetas. No pide contraseña. |
| `dist\Descifrar.exe` | Pide la contraseña y devuelve todo a la normalidad. |
| `keys\` | Copia de la llave del programa. **Guárdala en otro sitio.** |
| `src\` | Código fuente. |
| `build.ps1` | Recompila los dos `.exe`. |

### Uso normal

Hay un acceso directo en el Escritorio llamado **Cifrar mis carpetas** (apunta a
`dist\Cifrar.exe`).

1. Doble clic en **`Cifrar.exe`**.
2. En la primera ventana, pulsa **OK** o **Cancelar** para iniciar el cifrado.
   Ambos botones hacen lo mismo; cerrar la ventana con la **X** lo cancela.
3. Aparece la ventana principal para mostrar el progreso. Al terminar, aparece
   un mensaje. Las carpetas siguen en el Escritorio.
4. Para recuperar: doble clic en **`Descifrar.exe`**, escribir la contraseña
   y pulsar **Descifrar ahora**.

La ventana de cifrado muestra un contador visual de cuatro horas. Al llegar a
cero no inicia ni detiene ninguna operación; se reinicia la próxima vez que se
abre el programa.

Si ya están cifradas, `Cifrar.exe` no las vuelve a cifrar (avisa y no toca nada).
Si ya están descifradas, `Descifrar.exe` tampoco hace nada.

Si agregas archivos nuevos a una carpeta que ya está cifrada, `Cifrar.exe` te
avisará de que hay archivos sin cifrar. En ese caso: descifra la carpeta y vuelve
a cifrarla; así también quedará protegido lo nuevo.

---

## Cómo funciona (resumen técnico)

- **AES-256-GCM** por bloques de 1 MB para cada archivo. Cada bloque lleva su
  etiqueta de autenticación: si alguien modifica un solo byte, el descifrado
  falla y no se escribe basura.
- Cada carpeta usa una **clave de datos aleatoria** distinta.
- Esa clave se guarda envuelta con **RSA-3072-OAEP** en el archivo oculto
  `.seguro` de cada carpeta.
- `Cifrar.exe` solo contiene la **clave pública**: aunque alguien lo copie o lo
  analice, no puede descifrar nada.
- `Descifrar.exe` contiene la **clave privada cifrada** con tu contraseña:
  PBKDF2-HMAC-SHA256 con 1.000.000 de iteraciones + AES-256-GCM. La contraseña
  no está escrita en el programa.
- Se conservan los **nombres** de archivos y carpetas (solo se cifra el contenido).
- Los archivos vacíos, las subcarpetas y las carpetas vacías también se manejan.

---

## Seguridad: lo que de verdad importa

1. **No hay recuperación sin la contraseña ni sin `Descifrar.exe`.**
   Guarda una copia de `Descifrar.exe` y de la carpeta `keys\` en un USB o en tu
   nube personal. Si pierdes esas dos cosas, los archivos cifrados se pierden
   para siempre.
2. **No dejes `Descifrar.exe` a la vista** en el Escritorio. Muévelo a un USB o a
   una carpeta privada. `Cifrar.exe` sí puede quedarse en el Escritorio: no
   contiene secretos.
3. **La contraseña es la última barrera.** Si alguien encuentra `Descifrar.exe`,
   todavía necesita la contraseña. No la escribas en un papel pegado al monitor
   ni en un archivo dentro de la PC.
4. **Nombres de archivo visibles.** Quien abra la carpeta verá los nombres
   (`Contrato de alquiler.txt`, etc.) aunque no pueda leer el contenido. Si
   también quieres ocultar los nombres, habría que cambiar el diseño.
5. **OneDrive.** Tu Escritorio está sincronizado con OneDrive. Al cifrar, la nube
   puede conservar una **versión anterior sin cifrar** en el historial de
   versiones. Si eso te preocupa:
   - pausa OneDrive antes de cifrar, y
   - entra a `onedrive.com` y borra el historial de versiones de esos archivos.
   El programa te avisa de esto al terminar.
6. **SmartScreen.** Los `.exe` no están firmados digitalmente. Windows puede
   mostrar "Windows protegió tu PC". Pulsa **Más información → Ejecutar de todas
   formas**. También es normal que algún antivirus marque un programa que cifra
   archivos; es el comportamiento esperado de esta herramienta.

---

## Poner tu imagen en la ventana

1. Copia tu imagen a `src\imagen.png` (o `src\imagen.jpg`).
2. Ejecuta `build.ps1`.
3. La imagen queda incrustada dentro de los dos `.exe`; no hace falta ningún
   archivo adicional al lado.

Si no hay imagen, se dibuja un candado de ejemplo. También puedes dejar un
`imagen.png` junto a los `.exe`: el programa lo usa si no hay imagen incrustada.

## Cambiar el icono de la aplicación

El icono se genera automáticamente desde `app.ico.png` cada vez que ejecutas
`build.ps1`. Para cambiarlo, reemplaza `app.ico.png` por otro PNG (mejor cuadrado
y con fondo transparente) y vuelve a compilar. El generador crea `src\app.ico`
con tamaños de 16 a 256 píxeles y está en `tools\make_icon.ps1`.

---

## Cambiar la contraseña

Hay que hacerlo **con todo descifrado** y antes de volver a cifrar:

1. Descifra las tres carpetas.
2. Borra `src\keys.h` y la carpeta `keys\`.
3. Genera claves nuevas:
   ```
   build\keygen.exe
   ```
   (escribe la contraseña nueva y pulsa Enter).
4. Recompila:
   ```
   powershell -ExecutionPolicy Bypass -File build.ps1
   ```
5. Vuelve a cifrar.

Los datos cifrados con las claves anteriores **no** se pueden descifrar con las
nuevas. Por eso el paso 1 es obligatorio.

---

## Recompilar

Requisitos: Visual Studio Build Tools con el componente "Desarrollo para el
escritorio con C++" (ya está instalado en este equipo).

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

Los `.exe` quedan en `dist\`. Si todavía no existen las claves, `build.ps1`
compila `build\keygen.exe` y te pide ejecutarlo (escribes la contraseña y pulsas
Enter); después vuelve a ejecutar `build.ps1`.

### Pruebas

```powershell
build\test_vault.exe TU_CONTRASEÑA
powershell -ExecutionPolicy Bypass -File tools\gui_test.ps1
```

La primera prueba crea archivos temporales, cifra, comprueba que no se puedan
leer, descifra y compara byte por byte. La segunda prueba abre los dos `.exe`
reales, pulsa los botones y verifica las carpetas del Escritorio.

---

## Qué NO hacer

- No borres el archivo oculto `.seguro` de una carpeta cifrada: sin él no se
  puede recuperar la clave de esa carpeta.
- No muevas archivos cifrados a otra carpeta: el registro `.seguro` apunta a las
  rutas relativas.
- No ejecutes `Cifrar.exe` con las carpetas a medio descifrar. Si algo se
  interrumpe, vuelve a ejecutar `Descifrar.exe` (sabe continuar donde quedó).
- No cambies el nombre de las tres carpetas: el programa las busca por nombre
  exacto en el Escritorio.

---

## Estructura del proyecto

```
app de seguridad\
  src\                 código C++
    crypto.cpp/.h      AES-256-GCM, PBKDF2, RSA-OAEP (CNG de Windows)
    vault.cpp/.h       formato de archivo, registro .seguro, cifrar/descifrar
    ui.cpp/.h          ventana Win32, imagen, botón, barra de progreso
    encrypt_main.cpp   Cifrar.exe
    decrypt_main.cpp   Descifrar.exe
    keys.h             clave publica generada (NO editar a mano)
  tools\
    keygen.cpp         genera el par de claves
    test_vault.cpp     prueba automática
    gui_test.ps1       prueba de la interfaz
  keys\                copia de la llave (guardar aparte)
                       private_wrapped.bin se incrusta solo en Descifrar.exe
  build.ps1            compilador
  dist\                los dos .exe finales
```
