#include "gestor_almacenamiento.hpp"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <random>
#include <regex>
#include <set>
#include <sstream>

#include "tecflix/errores.hpp"

namespace tecflix::streaming {

namespace fs = std::filesystem;

namespace {

constexpr const char* kCarpetaSubidas = ".subidas";

std::string identificadorAleatorio() {
    static thread_local std::mt19937_64 generador{std::random_device{}()};
    std::ostringstream texto;
    texto << std::hex << generador() << generador();
    return texto.str();
}

}  // namespace

GestorAlmacenamiento::GestorAlmacenamiento(fs::path base) : base_(std::move(base)) {
    fs::create_directories(base_ / kCarpetaSubidas);
}

bool GestorAlmacenamiento::idValido(const std::string& id) {
    return id.size() == 24 &&
           std::all_of(id.begin(), id.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool GestorAlmacenamiento::nombreSegmentoValido(const std::string& nombre) {
    static const std::regex patron(R"(^(init|seg)-[0-9]+(-[0-9]{5})?\.m4s$)");
    return std::regex_match(nombre, patron);
}

std::optional<std::string> GestorAlmacenamiento::extensionPermitida(const std::string& nombreArchivo) {
    static const std::set<std::string> permitidas = {"mp4", "mov", "mkv", "webm", "avi"};
    const size_t punto = nombreArchivo.rfind('.');
    if (punto == std::string::npos) return std::nullopt;
    std::string extension = nombreArchivo.substr(punto + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (permitidas.count(extension) == 0) return std::nullopt;
    return extension;
}

fs::path GestorAlmacenamiento::nuevoArchivoTemporal() const {
    return base_ / kCarpetaSubidas / (identificadorAleatorio() + ".part");
}

fs::path GestorAlmacenamiento::carpetaVideo(const std::string& id) const {
    if (!idValido(id)) throw ErrorApi(TipoError::NoEncontrado, "El video no existe");
    return base_ / id;
}

fs::path GestorAlmacenamiento::moverOriginal(const fs::path& temporal, const std::string& id,
                                             const std::string& extension) const {
    const fs::path carpeta = carpetaVideo(id);
    fs::create_directories(carpeta);
    const fs::path destino = carpeta / ("original." + extension);
    fs::rename(temporal, destino);
    return destino;
}

fs::path GestorAlmacenamiento::rutaArchivo(const std::string& id, const std::string& nombre) const {
    if (nombre != "manifest.mpd" && !nombreSegmentoValido(nombre)) {
        throw ErrorApi(TipoError::NoEncontrado, "Archivo no encontrado");
    }
    return carpetaVideo(id) / nombre;
}

void GestorAlmacenamiento::eliminarVideo(const std::string& id) const { fs::remove_all(carpetaVideo(id)); }

int64_t GestorAlmacenamiento::tamanoCarpeta(const std::string& id) const {
    int64_t total = 0;
    std::error_code error;
    for (const auto& entrada : fs::recursive_directory_iterator(carpetaVideo(id), error)) {
        if (entrada.is_regular_file(error)) total += static_cast<int64_t>(entrada.file_size(error));
    }
    return total;
}

bool GestorAlmacenamiento::disponible() const {
    return fs::is_directory(base_) && access(base_.c_str(), W_OK) == 0;
}

}  // namespace tecflix::streaming
