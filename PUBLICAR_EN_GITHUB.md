# ORAS MMO — Publicar en GitHub (1 sola vez)

## Paso 1: Crear cuenta GitHub (si no tienes una)
1. Ve a https://github.com/signup
2. Crea tu cuenta gratis con tu email.

## Paso 2: Crear el repositorio
1. Entra a https://github.com/new
2. Ponle un nombre al repositorio, por ejemplo: `ORAS-MMO`
3. Déjalo en **Privado** (Private) si no quieres que lo vea nadie más.
4. Haz clic en **"Create repository"**

## Paso 3: Subir los archivos del proyecto
1. Abre PowerShell en Windows.
2. Pega estos comandos uno por uno:

```powershell
# Instalar git si no lo tienes
winget install Git.Git

# Ir a la carpeta del proyecto
cd "C:\Users\Tuf Gaming\.gemini\antigravity\scratch\ProjectORAS_MMO"

# Configurar git
git init
git add .
git commit -m "ORAS MMO Plugin v1.0"

# Conectar con tu repositorio (cambia TU_USUARIO por tu nombre de GitHub)
git remote add origin https://github.com/TU_USUARIO/ORAS-MMO.git
git push -u origin main
```

## Paso 4: Ver la compilación automática
1. Ve a tu repositorio en GitHub: https://github.com/TU_USUARIO/ORAS-MMO
2. Haz clic en la pestaña **"Actions"** (arriba en el menú)
3. Verás la compilación corriendo automáticamente (tarda ~5 minutos)
4. Cuando termine, haz clic en **"Releases"** en el panel lateral
5. Descarga el archivo `OrasMmo.3gx` desde ahí

¡Listo! Cada vez que yo actualice el código, solo tienes que hacer `git push` y se recompila solo.
