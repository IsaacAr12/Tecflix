#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "controlador_streaming.hpp"
#include "mocks_streaming.hpp"
#include "servidor_en_segundo_plano.hpp"

using namespace tecflix;
using namespace tecflix::streaming;
using nlohmann::json;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
namespace fs = std::filesystem;

namespace {

const std::string kId = "656f1a2b3c4d5e6f7a8b9c0d";
constexpr uint64_t kMaximo = 2000;

// Controlador real + servicio real + clientes y transcodificador simulados, probados por HTTP.
class ControladorStreamingTest : public ::testing::Test {
protected:
    CarpetaTemporal carpeta{"controlador"};
    GestorAlmacenamiento almacenamiento{carpeta.ruta()};
    NiceMock<MockClienteUsuarios> usuarios;
    NiceMock<MockClienteMetadata> metadata;
    NiceMock<MockTranscodificador> transcodificador;
    PoolTranscodificacion pool{1};
    ServicioStreaming servicio{almacenamiento, usuarios, metadata, transcodificador, pool, kMaximo};
    ServidorHttp servidor{"streaming-api", "1.0", "http://localhost:5173"};
    ControladorStreaming controlador{servicio, kMaximo};
    std::unique_ptr<pruebas::ServidorEnSegundoPlano> activo;
    std::unique_ptr<httplib::Client> cliente;

    void SetUp() override {
        controlador.registrarRutas(servidor.rutas());
        activo = std::make_unique<pruebas::ServidorEnSegundoPlano>(servidor);
        cliente = activo->cliente();
        ON_CALL(usuarios, obtenerCuota(_)).WillByDefault(Return(cuotaLibre(100000)));
        ON_CALL(usuarios, obtenerNombre(_)).WillByDefault(Return("isaac"));
        ON_CALL(metadata, crear(_)).WillByDefault(Return(kId));
        ON_CALL(transcodificador, transcodificar(_, _)).WillByDefault(Invoke(escribirPaqueteFalso));
    }

    void TearDown() override {
        activo.reset();
        pool.esperarVacia();
    }

    httplib::Result subir(const httplib::MultipartFormDataItems& partes, const std::string& usuario = "7") {
        httplib::Headers encabezados;
        if (!usuario.empty()) encabezados.emplace("X-Usuario-Id", usuario);
        return cliente->Post("/videos", encabezados, partes);
    }

    static httplib::MultipartFormDataItems formulario(const std::string& contenido,
                                                      const std::string& nombre = "clase.mp4") {
        return {{"titulo", "Clase", "", ""},
                {"tema", "Datos", "", ""},
                {"etiquetas", R"(["btree","disco"])", "", ""},
                {"visibilidad", "publico", "", ""},
                {"archivo", contenido, nombre, "video/mp4"}};
    }

    static std::string codigo(const httplib::Result& res) { return json::parse(res->body)["error"]["codigo"]; }
};

}  // namespace

TEST_F(ControladorStreamingTest, SubidaMultipartResponde202) {
    EXPECT_CALL(usuarios, registrarUso(7, 14));  // "datos-de-video" = 14 bytes
    EXPECT_CALL(metadata, crear(_)).WillOnce(Invoke([](const json& datos) {
        EXPECT_EQ(datos["titulo"], "Clase");
        EXPECT_EQ(datos["etiquetas"], json({"btree", "disco"}));
        EXPECT_EQ(datos["visibilidad"], "publico");
        return kId;
    }));
    EXPECT_CALL(metadata, actualizarProcesamiento(kId, _));
    auto res = subir(formulario("datos-de-video"));
    ASSERT_TRUE(res);
    ASSERT_EQ(res->status, 202) << res->body;
    EXPECT_EQ(json::parse(res->body), json({{"id_video", kId}, {"estado", "procesando"}}));
    pool.esperarVacia();
    EXPECT_TRUE(fs::exists(carpeta.ruta() / kId / "manifest.mpd"));
}

TEST_F(ControladorStreamingTest, ErroresDelFormulario) {
    EXPECT_CALL(usuarios, registrarUso(_, _)).Times(0);  // nada llega a reservar cuota
    EXPECT_EQ(codigo(subir(formulario("x"), "")), "NO_AUTENTICADO");
    EXPECT_EQ(codigo(subir(formulario("x", "script.sh"))), "DATOS_INVALIDOS");

    httplib::MultipartFormDataItems archivoPrimero = {{"archivo", "x", "clase.mp4", "video/mp4"},
                                                      {"titulo", "Clase", "", ""}};
    EXPECT_EQ(codigo(subir(archivoPrimero)), "DATOS_INVALIDOS");

    httplib::MultipartFormDataItems desconocido = {{"autor_id", "1", "", ""}};
    EXPECT_EQ(codigo(subir(desconocido)), "DATOS_INVALIDOS");

    httplib::MultipartFormDataItems repetido = {{"titulo", "a", "", ""}, {"titulo", "b", "", ""}};
    EXPECT_EQ(codigo(subir(repetido)), "DATOS_INVALIDOS");

    httplib::MultipartFormDataItems largo = {{"titulo", std::string(11 * 1024, 'x'), "", ""}};
    EXPECT_EQ(subir(largo)->status, 400);

    httplib::MultipartFormDataItems sinArchivo = {{"titulo", "Clase", "", ""}};
    EXPECT_EQ(codigo(subir(sinArchivo)), "DATOS_INVALIDOS");

    auto etiquetasMalas = formulario("x");
    etiquetasMalas[2].content = "btree,disco";
    EXPECT_EQ(codigo(subir(etiquetasMalas)), "DATOS_INVALIDOS");
    etiquetasMalas[2].content = "[1,2]";
    EXPECT_EQ(codigo(subir(etiquetasMalas)), "DATOS_INVALIDOS");

    auto res = cliente->Post("/videos", {{"X-Usuario-Id", "7"}}, R"({"titulo":"x"})", "application/json");
    EXPECT_EQ(codigo(res), "DATOS_INVALIDOS");
}

TEST_F(ControladorStreamingTest, LimitesDeTamanoYCuota) {
    auto res = subir(formulario(std::string(kMaximo + ServicioStreaming::kMargenMultipart, 'x')));
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 413);
    EXPECT_EQ(codigo(res), "ARCHIVO_DEMASIADO_GRANDE");

    EXPECT_CALL(usuarios, obtenerCuota(7))
        .WillOnce(Return(cuotaLibre(10)))
        .WillRepeatedly(Return(cuotaLibre(100000)));
    httplib::Headers encabezados = {{"X-Usuario-Id", "7"}};
    // Content-Length mayor que lo disponible + margen: se rechaza sin leer el cuerpo.
    res = cliente->Post("/videos", encabezados, std::string(ServicioStreaming::kMargenMultipart + 100, 'x'),
                        "multipart/form-data; boundary=abc");
    EXPECT_EQ(codigo(res), "CUOTA_EXCEDIDA");

    // Archivo que pasa el chequeo previo pero supera MAX_BYTES_SUBIDA mientras se recibe.
    res = subir(formulario(std::string(kMaximo + 10, 'x')));
    EXPECT_EQ(codigo(res), "ARCHIVO_DEMASIADO_GRANDE");

    EXPECT_CALL(usuarios, registrarUso(7, 5)).WillOnce(::testing::Throw(ErrorApi(TipoError::CuotaExcedida, "sin espacio")));
    EXPECT_EQ(codigo(subir(formulario("12345"))), "CUOTA_EXCEDIDA");
}

TEST_F(ControladorStreamingTest, ReproduccionConRangeYSalud) {
    fs::create_directories(carpeta.ruta() / kId);
    escribirPaqueteFalso({}, carpeta.ruta() / kId);
    ON_CALL(metadata, obtener(kId, 9)).WillByDefault(Return(infoVideo(kId, 7, "listo")));

    auto res = cliente->Get("/videos/" + kId + "/manifest.mpd", {{"X-Usuario-Id", "9"}});
    ASSERT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("Content-Type"), "application/dash+xml");
    EXPECT_EQ(res->body, "<MPD></MPD>");

    res = cliente->Get("/videos/" + kId + "/seg-0-00001.m4s", {{"X-Usuario-Id", "9"}, {"Range", "bytes=2-5"}});
    ASSERT_EQ(res->status, 206);
    EXPECT_EQ(res->body, "2345");
    EXPECT_EQ(res->get_header_value("Content-Type"), "video/iso.segment");
    EXPECT_EQ(res->get_header_value("Cache-Control"), "private, max-age=3600");

    EXPECT_EQ(cliente->Get("/videos/" + kId + "/manifest.mpd")->status, 401);
    EXPECT_EQ(cliente->Get("/videos/" + kId + "/original.mp4", {{"X-Usuario-Id", "9"}})->status, 404);
    EXPECT_EQ(cliente->Get("/salud")->status, 200);
}

TEST_F(ControladorStreamingTest, Eliminar) {
    EXPECT_CALL(metadata, obtener(kId, 7)).WillOnce(Return(infoVideo(kId, 7, "listo", 50)));
    EXPECT_CALL(metadata, eliminar(kId));
    EXPECT_CALL(usuarios, registrarUso(7, -50));
    EXPECT_EQ(cliente->Delete("/videos/" + kId, {{"X-Usuario-Id", "7"}})->status, 204);

    EXPECT_CALL(metadata, obtener(kId, 9)).WillOnce(Return(infoVideo(kId, 7, "listo")));
    auto res = cliente->Delete("/videos/" + kId, {{"X-Usuario-Id", "9"}});
    EXPECT_EQ(res->status, 403);
    EXPECT_EQ(codigo(res), "SIN_PERMISO");
}

TEST(ControladorStreamingEstatico, EtiquetasYBytesEnviados) {
    EXPECT_EQ(ControladorStreaming::parsearEtiquetas(R"(["a","b"])"), (std::vector<std::string>{"a", "b"}));
    EXPECT_THROW(ControladorStreaming::parsearEtiquetas("a,b"), ErrorApi);

    httplib::Request req;
    EXPECT_EQ(ControladorStreaming::bytesEnviados(req, 100), 100u);
    req.ranges = {{10, 19}};
    EXPECT_EQ(ControladorStreaming::bytesEnviados(req, 100), 10u);
    req.ranges = {{90, -1}};
    EXPECT_EQ(ControladorStreaming::bytesEnviados(req, 100), 10u);
    req.ranges = {{-1, 30}};
    EXPECT_EQ(ControladorStreaming::bytesEnviados(req, 100), 30u);
    req.ranges = {{200, 300}};
    EXPECT_EQ(ControladorStreaming::bytesEnviados(req, 100), 0u);
    req.ranges = {{0, 500}};
    EXPECT_EQ(ControladorStreaming::bytesEnviados(req, 100), 100u);
}
