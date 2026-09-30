# Código-fonte do core PlayStation 1 usado pelo EmuOrbit Advance

Este repositório publica o código-fonte correspondente ao **core PCSX ReARMed
(Libretro)** escolhido para a integração experimental de PlayStation 1 do
EmuOrbit Advance. O snapshot em `core/` é o commit upstream
`ff81ed17a15241d2f3730cdd7585b38e172532ca` de
[`libretro/pcsx_rearmed`](https://github.com/libretro/pcsx_rearmed). Há **uma
alteração de build** em `core/jni/Android.mk`: ela injeta a identificação do
commit upstream quando o snapshot é compilado fora do histórico Git original.
Nenhuma lógica de emulação foi alterada. Não há ROMs, BIOS, saves, APKs, chaves
ou imagens do aplicativo neste repositório.

O PCSX ReARMed é identificado pelos mantenedores como **GPLv2**. A licença
integral está em [`core/COPYING`](core/COPYING); os componentes vendorizados no
core mantêm seus próprios avisos e licenças por arquivo/diretório. Consulte
[`NOTICES.md`](NOTICES.md) antes de redistribuir binários.

## Build do core Android ARM64

No Windows, instale o Android NDK `29.0.14206865` e o Git for Windows. Execute
em um caminho sem espaços:

```powershell
./build-arm64.ps1
```

O script usa API 26, `arm64-v8a`, `SOURCE_DATE_EPOCH=1790204415`, flags
`USE_ASYNC_CDROM=0`, `USE_ASYNC_GPU=0`, `USE_ASYNC_SPU=0`, `NDRC_THREAD=0` e
`USE_LIBRETRO_VFS=1`, além da identificação upstream `r26l-112-gff81ed1`.
O resultado fica em `out/libs/arm64-v8a/libretro.so`.
A receita é isolada: não requer nem publica o código da interface, as ROMs ou
a BIOS do app. O próprio `core/jni/Android.mk` e os arquivos incluídos no
snapshot documentam a compilação upstream.

O checkout exclui apenas arquivos opcionais de Pandora, ferramentas de desktop
e o submódulo `libpicofe`, todos fora desta receita Android. O cabeçalho
`core/include/revision.h` é gerado pelo build; os 208 arquivos de código e
cabeçalhos usados pela compilação ARM64 foram conferidos contra o inventário
do projeto (o único arquivo não versionado é esse cabeçalho gerado).

O build ARM64 do snapshot foi executado com essa receita. O código upstream
contém um caminho de arquivo em mensagens do binário; portanto, compilar em
diretórios diferentes pode produzir SHA-256 diferente sem mudar a revisão ou
as opções de compilação. O script imprime o hash do resultado local para
conferência, sem prometer igualdade byte a byte entre máquinas.

Esta publicação cobre **este core e esta revisão**, não certifica por si só
que um APK futuro, suas outras bibliotecas ou sua forma de distribuição
cumprem todas as licenças. Antes de distribuir uma versão com PS1 é preciso
conferir a correspondência entre o binário efetivamente incluído e esta fonte,
os avisos de terceiros e as obrigações da combinação final.
