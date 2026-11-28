#ifndef STREAMING_CLIENTE_USUARIOS_HPP
#define STREAMING_CLIENTE_USUARIOS_HPP

#include <cstdint>
#include <string>

#include "tecflix/cliente_http_interno.hpp"

namespace tecflix::streaming {

struct InfoCuota {
    uint64_t cuotaBytes = 0;
    uint64_t usadoBytes = 0;
    uint64_t disponibleBytes = 0;
};

// Comunicación con el API de usuarios (SPEC 6.9). Interfaz para poder simularla en las pruebas.
class IClienteUsuarios {
public:
    virtual ~IClienteUsuarios() = default;

    // GET /usuarios/{id}/cuota
    virtual InfoCuota obtenerCuota(uint32_t usuarioId) = 0;

    // GET /usuarios/{id} -> campo "nombre_usuario" (se copia como autor_nombre en la metadata).
    virtual std::string obtenerNombre(uint32_t usuarioId) = 0;

    // POST /usuarios/{id}/uso con {"delta_bytes": delta}. Positivo reserva, negativo libera.
    // Lanza ErrorApi(CuotaExcedida) si la reserva no cabe.
    virtual void registrarUso(uint32_t usuarioId, int64_t deltaBytes) = 0;
};

class ClienteUsuariosHttp : public IClienteUsuarios {
public:
    ClienteUsuariosHttp(std::string url, std::string tokenInterno);

    InfoCuota obtenerCuota(uint32_t usuarioId) override;
    std::string obtenerNombre(uint32_t usuarioId) override;
    void registrarUso(uint32_t usuarioId, int64_t deltaBytes) override;

private:
    ClienteHttpInterno http_;
};

}  // namespace tecflix::streaming

#endif  // STREAMING_CLIENTE_USUARIOS_HPP
