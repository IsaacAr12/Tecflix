#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>

#include "ejecutor_procesos.hpp"
#include "gestor_almacenamiento.hpp"
#include "mocks_streaming.hpp"
#include "tecflix/errores.hpp"
#include "transcodificador.hpp"

using namespace tecflix;
using namespace tecflix::streaming;
namespace fs = std::filesystem;

namespace {

bool contiene(const std::vector<std::string>& args, const std::string& valor) {
    return std::find(args.begin(), args.end(), valor) != args.end();
}

std::string leerArchivo(const fs::path& ruta) {
    std::ifstream archivo(ruta);
    return std::string((std::istreambuf_iterator<char>(archivo)), std::istreambuf_iterator<char>());
}

// Video sintético de 3 s generado con los filtros lavfi de FFMPEG (no hay videos en el repositorio).
fs::path generarVideo(const fs::path& carpeta, int alto, bool conAudio) {
    const fs::path salida = carpeta / "original.mp4";
    const int ancho = alto * 16 / 9 / 2 * 2;
    std::vector<std::string> args = {"ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                                     "testsrc=duration=3:size=" + std::to_string(ancho) + "x" + std::to_string(alto) +
                                         ":rate=30"};
    if (conAudio) args.insert(args.end(), {"-f", "lavfi", "-i", "sine=frequency=440:duration=3"});
    args.insert(args.end(), {"-c:v", "libx264", "-preset", "ultrafast", "-pix_fmt", "yuv420p"});
    if (conAudio) args.insert(args.end(), {"-c:a", "aac", "-shortest"});
    args.push_back(salida.string());
    const ResultadoProceso resultado = EjecutorProcesos::ejecutar(args, true);
    EXPECT_EQ(resultado.codigoSalida, 0) << resultado.salida;
    return salida;
}

}  // namespace

TEST(EjecutorProcesos, CapturaSalidaYCodigo) {
    ResultadoProceso r = EjecutorProcesos::ejecutar({"sh", "-c", "echo hola; echo error 1>&2; exit 3"}, true);
    EXPECT_EQ(r.codigoSalida, 3);
    EXPECT_NE(r.salida.find("hola"), std::string::npos);
    EXPECT_NE(r.salida.find("error"), std::string::npos);
    r = EjecutorProcesos::ejecutar({"sh", "-c", "echo hola; echo error 1>&2"}, false);
    EXPECT_EQ(r.salida, "hola\n");
}

TEST(EjecutorProcesos, NoPasaPorUnaShell) {
    // Con system() esto ejecutaría "touch" (inyección de comandos, SPEC 8.4).
    const fs::path marca = fs::temp_directory_path() / "tecflix_no_debe_existir";
    fs::remove(marca);
    EjecutorProcesos::ejecutar({"echo", "; touch " + marca.string()}, false);
    EXPECT_FALSE(fs::exists(marca));
}

TEST(EjecutorProcesos, RecortaSalidasEnormesYReportaErrores) {
    ResultadoProceso r = EjecutorProcesos::ejecutar({"sh", "-c", "head -c 3000000 /dev/zero; echo FIN"}, false);
    EXPECT_EQ(r.salida.size(), EjecutorProcesos::kMaximoSalida);
    EXPECT_EQ(r.salida.substr(r.salida.size() - 4), "FIN\n");
    EXPECT_THROW(EjecutorProcesos::ejecutar({}, false), std::invalid_argument);
    EXPECT_THROW(EjecutorProcesos::ejecutar({"programa-que-no-existe-tecflix"}, false), std::runtime_error);
}

TEST(TranscodificadorFfmpeg, EscaleraDelSpec) {
    auto alturas = [](int alto) {
        std::vector<int> r;
        for (const Rendicion& x : TranscodificadorFfmpeg::escalera(alto)) r.push_back(x.alto);
        return r;
    };
    EXPECT_EQ(alturas(2160), (std::vector<int>{1080, 720, 360}));
    EXPECT_EQ(alturas(1080), (std::vector<int>{1080, 720, 360}));
    EXPECT_EQ(alturas(720), (std::vector<int>{720, 360}));
    EXPECT_EQ(alturas(480), (std::vector<int>{360}));
    EXPECT_EQ(alturas(241), (std::vector<int>{240}));  // menos de 360: su altura, par
    EXPECT_EQ(TranscodificadorFfmpeg::escalera(241)[0].kbps, 500);
}

TEST(TranscodificadorFfmpeg, ParseaFfprobe) {
    InfoMedio info = TranscodificadorFfmpeg::parsearFfprobe(R"({
        "streams": [
            {"codec_type": "video", "height": 50, "disposition": {"attached_pic": 1}},
            {"codec_type": "video", "height": 720},
            {"codec_type": "audio"}],
        "format": {"duration": "12.5"}})");
    EXPECT_TRUE(info.tieneVideo);
    EXPECT_TRUE(info.tieneAudio);
    EXPECT_EQ(info.alto, 720);
    EXPECT_DOUBLE_EQ(info.duracionSeg, 12.5);
    info = TranscodificadorFfmpeg::parsearFfprobe(R"({"streams": [], "format": {"duration": 3}})");
    EXPECT_FALSE(info.tieneVideo);
    EXPECT_DOUBLE_EQ(info.duracionSeg, 3.0);
    EXPECT_THROW(TranscodificadorFfmpeg::parsearFfprobe("no es json"), ErrorApi);
}

TEST(TranscodificadorFfmpeg, ArgumentosSinShellYConLosNombresDelSpec) {
    InfoMedio info;
    info.tieneVideo = true;
    info.tieneAudio = true;
    auto args = TranscodificadorFfmpeg::argumentosDash("ffmpeg", "/in.mp4", "/out", info, {{720, 2800}, {360, 800}});
    EXPECT_EQ(args.back(), "/out/manifest.mpd");
    EXPECT_TRUE(contiene(args, "[0:v]split=2[s0][s1];[s0]scale=-2:720[v0];[s1]scale=-2:360[v1]"));
    EXPECT_TRUE(contiene(args, "seg-$RepresentationID$-$Number%05d$.m4s"));
    EXPECT_TRUE(contiene(args, "expr:gte(t,n_forced*4)"));
    EXPECT_TRUE(contiene(args, "id=0,streams=v id=1,streams=a"));
    info.tieneAudio = false;
    args = TranscodificadorFfmpeg::argumentosDash("ffmpeg", "/in.mp4", "/out", info, {{360, 800}});
    EXPECT_FALSE(contiene(args, "0:a:0"));
    EXPECT_TRUE(contiene(args, "id=0,streams=v"));
}

TEST(TranscodificadorFfmpeg, GeneraDashConAudioYVariasCalidades) {
    CarpetaTemporal carpeta("tx_audio");
    const fs::path original = generarVideo(carpeta.ruta(), 720, true);
    TranscodificadorFfmpeg transcodificador;
    EXPECT_TRUE(transcodificador.disponible());

    const ResultadoTranscodificacion r = transcodificador.transcodificar(original, carpeta.ruta());
    EXPECT_NEAR(r.duracionSeg, 3.0, 0.2);
    EXPECT_EQ(r.alturas, (std::vector<int>{720, 360}));
    for (const char* archivo : {"manifest.mpd", "init-0.m4s", "init-1.m4s", "init-2.m4s", "seg-0-00001.m4s"}) {
        EXPECT_TRUE(fs::exists(carpeta.ruta() / archivo)) << archivo;
        if (std::string(archivo) != "manifest.mpd") {
            EXPECT_TRUE(GestorAlmacenamiento::nombreSegmentoValido(archivo)) << archivo;
        }
    }
    const std::string manifiesto = leerArchivo(carpeta.ruta() / "manifest.mpd");
    EXPECT_NE(manifiesto.find("audio/mp4"), std::string::npos);
    EXPECT_NE(manifiesto.find("height=\"720\""), std::string::npos);
}

TEST(TranscodificadorFfmpeg, VideoPequenoSinAudio) {
    CarpetaTemporal carpeta("tx_sin_audio");
    const fs::path original = generarVideo(carpeta.ruta(), 240, false);
    TranscodificadorFfmpeg transcodificador;
    const ResultadoTranscodificacion r = transcodificador.transcodificar(original, carpeta.ruta());
    EXPECT_EQ(r.alturas, (std::vector<int>{240}));
    EXPECT_EQ(leerArchivo(carpeta.ruta() / "manifest.mpd").find("audio/mp4"), std::string::npos);
}

TEST(TranscodificadorFfmpeg, RechazaLoQueNoEsVideo) {
    CarpetaTemporal carpeta("tx_malo");
    std::ofstream(carpeta.ruta() / "texto.mp4") << "esto no es un video";
    TranscodificadorFfmpeg transcodificador;
    try {
        transcodificador.transcodificar(carpeta.ruta() / "texto.mp4", carpeta.ruta());
        FAIL();
    } catch (const ErrorApi& e) {
        EXPECT_EQ(e.tipo(), TipoError::DatosInvalidos);
    }
    EjecutorProcesos::ejecutar({"ffmpeg", "-loglevel", "error", "-y", "-f", "lavfi", "-i", "sine=duration=1",
                                (carpeta.ruta() / "audio.m4a").string()},
                               true);
    EXPECT_THROW(transcodificador.transcodificar(carpeta.ruta() / "audio.m4a", carpeta.ruta()), ErrorApi);
}

TEST(TranscodificadorFfmpeg, FallaDeFfmpeg) {
    CarpetaTemporal carpeta("tx_falla");
    const fs::path original = generarVideo(carpeta.ruta(), 240, false);
    TranscodificadorFfmpeg roto("false", "ffprobe");  // "false" termina con código 1
    EXPECT_FALSE(roto.disponible());
    EXPECT_THROW(roto.transcodificar(original, carpeta.ruta()), ErrorApi);
    TranscodificadorFfmpeg inexistente("ffmpeg-que-no-existe-tecflix");
    EXPECT_FALSE(inexistente.disponible());
}
