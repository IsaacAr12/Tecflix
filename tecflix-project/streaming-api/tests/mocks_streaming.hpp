#ifndef STREAMING_TESTS_MOCKS_STREAMING_HPP
#define STREAMING_TESTS_MOCKS_STREAMING_HPP

#include <gmock/gmock.h>
#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <fstream>

#include "cliente_metadata.hpp"
#include "cliente_usuarios.hpp"
#include "transcodificador.hpp"

namespace tecflix::streaming {

// Mocks de GoogleMock de las dependencias del servicio (SPEC 8.5).
class MockClienteUsuarios : public IClienteUsuarios {
public:
    MOCK_METHOD(InfoCuota, obtenerCuota, (uint32_t usuarioId), (override));
    MOCK_METHOD(std::string, obtenerNombre, (uint32_t usuarioId), (override));
    MOCK_METHOD(void, registrarUso, (uint32_t usuarioId, int64_t deltaBytes), (override));
};

class MockClienteMetadata : public IClienteMetadata {
public:
    MOCK_METHOD(std::string, crear, (const nlohmann::json& datos), (override));
    MOCK_METHOD(std::optional<InfoVideo>, obtener, (const std::string& id, uint32_t usuarioId), (override));
    MOCK_METHOD(void, actualizarProcesamiento, (const std::string& id, const nlohmann::json& cambios), (override));
    MOCK_METHOD(void, eliminar, (const std::string& id), (override));
};

class MockTranscodificador : public ITranscodificador {
public:
    MOCK_METHOD(ResultadoTranscodificacion, transcodificar,
                (const std::filesystem::path& original, const std::filesystem::path& carpetaSalida), (override));
};

// Acción para el mock: escribe un paquete DASH mínimo, como haría FFMPEG.
inline ResultadoTranscodificacion escribirPaqueteFalso(const std::filesystem::path&,
                                                       const std::filesystem::path& carpeta) {
    std::ofstream(carpeta / "manifest.mpd") << "<MPD></MPD>";
    std::ofstream(carpeta / "init-0.m4s") << "init";
    std::ofstream(carpeta / "seg-0-00001.m4s") << "0123456789";
    return {12.0, {720, 360}};
}

inline InfoVideo infoVideo(const std::string& id, uint32_t autor, const std::string& estado, int64_t tamano = 100) {
    return InfoVideo{id, autor, estado, tamano};
}

inline InfoCuota cuotaLibre(uint64_t disponible) { return {disponible, 0, disponible}; }

// Carpeta temporal única por prueba, que se borra al terminar.
class CarpetaTemporal {
public:
    explicit CarpetaTemporal(const std::string& nombre) {
        static std::atomic<int> contador{0};
        ruta_ = std::filesystem::temp_directory_path() /
                ("tecflix_" + nombre + "_" + std::to_string(::getpid()) + "_" + std::to_string(++contador));
        std::filesystem::remove_all(ruta_);
        std::filesystem::create_directories(ruta_);
    }
    ~CarpetaTemporal() {
        std::error_code ignorado;
        std::filesystem::remove_all(ruta_, ignorado);
    }
    CarpetaTemporal(const CarpetaTemporal&) = delete;
    CarpetaTemporal& operator=(const CarpetaTemporal&) = delete;
    const std::filesystem::path& ruta() const { return ruta_; }

private:
    std::filesystem::path ruta_;
};

}  // namespace tecflix::streaming

#endif  // STREAMING_TESTS_MOCKS_STREAMING_HPP
