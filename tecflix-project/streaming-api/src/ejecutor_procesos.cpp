#include "ejecutor_procesos.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

extern char** environ;

namespace tecflix::streaming {

namespace {

// Cierra el descriptor al salir de alcance.
class Descriptor {
public:
    explicit Descriptor(int fd = -1) : fd_(fd) {}
    ~Descriptor() { cerrar(); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    int get() const { return fd_; }
    void cerrar() {
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
    }

private:
    int fd_;
};

}  // namespace

ResultadoProceso EjecutorProcesos::ejecutar(const std::vector<std::string>& argumentos, bool combinarStderr) {
    if (argumentos.empty()) throw std::invalid_argument("No hay programa que ejecutar");

    int extremos[2];
    if (pipe2(extremos, O_CLOEXEC) != 0) throw std::runtime_error(std::string("pipe: ") + std::strerror(errno));
    Descriptor lectura(extremos[0]);
    Descriptor escritura(extremos[1]);

    posix_spawn_file_actions_t acciones;
    posix_spawn_file_actions_init(&acciones);
    posix_spawn_file_actions_addopen(&acciones, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&acciones, escritura.get(), STDOUT_FILENO);
    if (combinarStderr) {
        posix_spawn_file_actions_adddup2(&acciones, escritura.get(), STDERR_FILENO);
    } else {
        posix_spawn_file_actions_addopen(&acciones, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    }

    std::vector<char*> argv;
    argv.reserve(argumentos.size() + 1);
    for (const auto& argumento : argumentos) argv.push_back(const_cast<char*>(argumento.c_str()));
    argv.push_back(nullptr);

    pid_t pid = 0;
    const int error = posix_spawnp(&pid, argv[0], &acciones, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&acciones);
    escritura.cerrar();  // si no se cierra aquí, la lectura nunca vería el fin de archivo
    if (error != 0) {
        throw std::runtime_error("No se pudo ejecutar " + argumentos[0] + ": " + std::strerror(error));
    }

    ResultadoProceso resultado;
    char bloque[8192];
    ssize_t leidos = 0;
    while ((leidos = read(lectura.get(), bloque, sizeof(bloque))) != 0) {
        if (leidos < 0) {
            if (errno == EINTR) continue;
            break;
        }
        resultado.salida.append(bloque, static_cast<size_t>(leidos));
        if (resultado.salida.size() > kMaximoSalida) {
            // Se conserva el final, que es donde ffmpeg escribe el error.
            resultado.salida.erase(0, resultado.salida.size() - kMaximoSalida);
        }
    }

    int estado = 0;
    while (waitpid(pid, &estado, 0) < 0 && errno == EINTR) {
    }
    resultado.codigoSalida = WIFEXITED(estado) ? WEXITSTATUS(estado) : -1;
    return resultado;
}

}  // namespace tecflix::streaming
