#ifndef STREAMING_EJECUTOR_PROCESOS_HPP
#define STREAMING_EJECUTOR_PROCESOS_HPP

#include <string>
#include <vector>

namespace tecflix::streaming {

struct ResultadoProceso {
    int codigoSalida = -1;
    std::string salida;  // stdout (y stderr si se pidió combinarlos), recortado a un máximo
};

// Ejecuta programas externos (ffmpeg, ffprobe) sin pasar por una shell: los
// argumentos van tal cual a posix_spawn, así un nombre de archivo nunca se
// interpreta como comando.
class EjecutorProcesos {
public:
    static constexpr size_t kMaximoSalida = 1 << 20;  // 1 MiB

    static ResultadoProceso ejecutar(const std::vector<std::string>& argumentos, bool combinarStderr);
};

}  // namespace tecflix::streaming

#endif  // STREAMING_EJECUTOR_PROCESOS_HPP
