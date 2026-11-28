#ifndef STREAMING_GESTOR_ALMACENAMIENTO_HPP
#define STREAMING_GESTOR_ALMACENAMIENTO_HPP

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace tecflix::streaming {

// Archivos en disco (SPEC 8.1):
//   <DIR_VIDEOS>/.subidas/<aleatorio>.part   archivo mientras se recibe (aún no tiene id)
//   <DIR_VIDEOS>/{id}/original.<ext>          se borra tras transcodificar correctamente
//   <DIR_VIDEOS>/{id}/manifest.mpd, init-N.m4s, seg-N-00001.m4s ...
class GestorAlmacenamiento {
public:
    explicit GestorAlmacenamiento(std::filesystem::path base);

    // {id}: ObjectId de 24 caracteres hexadecimales.
    static bool idValido(const std::string& id);
    // ^(init|seg)-[0-9]+(-[0-9]{5})?\.m4s$  (SPEC 8.4). Evita el path traversal.
    static bool nombreSegmentoValido(const std::string& nombre);
    // Extensión en minúsculas si está en la lista blanca (mp4, mov, mkv, webm, avi).
    static std::optional<std::string> extensionPermitida(const std::string& nombreArchivo);

    // Ruta nueva y única para recibir una subida.
    std::filesystem::path nuevoArchivoTemporal() const;
    // Mueve la subida a <id>/original.<ext> y devuelve la ruta final.
    std::filesystem::path moverOriginal(const std::filesystem::path& temporal, const std::string& id,
                                        const std::string& extension) const;

    std::filesystem::path carpetaVideo(const std::string& id) const;
    // manifest.mpd o un segmento válido; lanza ErrorApi(NoEncontrado) si el nombre no es válido.
    std::filesystem::path rutaArchivo(const std::string& id, const std::string& nombre) const;

    void eliminarVideo(const std::string& id) const;
    int64_t tamanoCarpeta(const std::string& id) const;
    // true si la carpeta base existe y se puede escribir (para GET /salud).
    bool disponible() const;

private:
    std::filesystem::path base_;
};

}  // namespace tecflix::streaming

#endif  // STREAMING_GESTOR_ALMACENAMIENTO_HPP
