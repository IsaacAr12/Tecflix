#ifndef STREAMING_POOL_TRANSCODIFICACION_HPP
#define STREAMING_POOL_TRANSCODIFICACION_HPP

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace tecflix::streaming {

// Cola de trabajos con un número fijo de hilos. La transcodificación tarda
// minutos y consume mucho CPU, por eso no se hace en el hilo de la petición HTTP
// ni se lanzan procesos sin límite: la subida responde 202 y el trabajo espera turno.
class PoolTranscodificacion {
public:
    using Trabajo = std::function<void()>;

    explicit PoolTranscodificacion(size_t hilos);
    ~PoolTranscodificacion();  // termina los trabajos que están corriendo y descarta los pendientes

    PoolTranscodificacion(const PoolTranscodificacion&) = delete;
    PoolTranscodificacion& operator=(const PoolTranscodificacion&) = delete;

    void encolar(Trabajo trabajo);
    size_t pendientes() const;  // en espera + en ejecución
    void esperarVacia();        // bloquea hasta que no quede trabajo (útil en pruebas)

private:
    void ciclo();

    mutable std::mutex mutex_;
    std::condition_variable hayTrabajo_;
    std::condition_variable vacia_;
    std::queue<Trabajo> cola_;
    size_t enEjecucion_ = 0;
    bool deteniendo_ = false;
    std::vector<std::thread> hilos_;
};

}  // namespace tecflix::streaming

#endif  // STREAMING_POOL_TRANSCODIFICACION_HPP
