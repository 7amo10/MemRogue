/**
 * @file webserver_sim.c
 * @brief Web Server Simulation - Realistic Memory Leak Scenario
 *
 * This example simulates a web server handling HTTP requests, managing
 * connection pools, and tracking user sessions. It demonstrates realistic
 * memory leak scenarios that commonly occur in server applications.
 *
 * Leak scenarios demonstrated:
 * 1. Unfreed request buffers on error paths
 * 2. Session data not cleaned up on timeout
 * 3. Connection pool exhaustion without cleanup
 * 4. Response buffer leaks on premature client disconnect
 *
 * Usage with MemRogue:
 *   LD_PRELOAD=./lib/libmemrogue_intercept.so ./bin/webserver_sim
 *
 * @author MemRogue Team
 * @date 2024
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

/* ==========================================================================
 * Configuration
 * ========================================================================== */

#define MAX_CONNECTIONS     32
#define MAX_SESSIONS        64
#define REQUEST_BUFFER_SIZE 4096
#define RESPONSE_BUFFER_SIZE 8192
#define SESSION_TIMEOUT_SEC 30
#define NUM_WORKER_THREADS  4
#define SIMULATION_REQUESTS 100

/* ==========================================================================
 * Data Structures
 * ========================================================================== */

/**
 * @brief HTTP request structure
 */
typedef struct {
    char *method;
    char *path;
    char *headers;
    char *body;
    size_t body_length;
    char *raw_buffer;
} http_request_t;

/**
 * @brief HTTP response structure
 */
typedef struct {
    int status_code;
    char *headers;
    char *body;
    size_t body_length;
} http_response_t;

/**
 * @brief User session structure
 */
typedef struct {
    uint64_t session_id;
    char *username;
    char *session_data;
    time_t created_at;
    time_t last_access;
    bool active;
} session_t;

/**
 * @brief Connection structure
 */
typedef struct {
    int fd;                     /* Simulated file descriptor */
    bool in_use;
    http_request_t *request;
    http_response_t *response;
    session_t *session;
    time_t connected_at;
} connection_t;

/**
 * @brief Connection pool structure
 */
typedef struct {
    connection_t connections[MAX_CONNECTIONS];
    pthread_mutex_t lock;
    int active_count;
} connection_pool_t;

/**
 * @brief Session store structure
 */
typedef struct {
    session_t sessions[MAX_SESSIONS];
    pthread_mutex_t lock;
    uint64_t next_session_id;
} session_store_t;

/**
 * @brief Request queue item
 */
typedef struct request_queue_item {
    connection_t *conn;
    struct request_queue_item *next;
} request_queue_item_t;

/**
 * @brief Request queue structure
 */
typedef struct {
    request_queue_item_t *head;
    request_queue_item_t *tail;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    bool shutdown;
} request_queue_t;

/* ==========================================================================
 * Global State
 * ========================================================================== */

static connection_pool_t g_pool;
static session_store_t g_sessions;
static request_queue_t g_request_queue;
static int g_shutdown = 0;

/* Statistics */
static int g_requests_processed = 0;
static int g_requests_failed = 0;
static pthread_mutex_t g_stats_lock = PTHREAD_MUTEX_INITIALIZER;

/* ==========================================================================
 * HTTP Request/Response Functions
 * ========================================================================== */

/**
 * @brief Parse an HTTP request from raw buffer
 * @param raw_data Raw HTTP request data
 * @return Parsed request or NULL on failure
 */
static http_request_t *parse_request(const char *raw_data) {
    http_request_t *req = malloc(sizeof(http_request_t));
    if (!req) {
        return NULL;
    }

    memset(req, 0, sizeof(http_request_t));

    /* Store raw buffer copy */
    req->raw_buffer = strdup(raw_data);
    if (!req->raw_buffer) {
        free(req);
        return NULL;
    }

    /* Parse method (simplified) */
    const char *space = strchr(raw_data, ' ');
    if (!space) {
        /* LEAK SCENARIO 1: Error path leak - raw_buffer not freed */
        /* In real code, this should free raw_buffer before freeing req */
        free(req);
        return NULL;
    }

    size_t method_len = (size_t)(space - raw_data);
    req->method = malloc(method_len + 1);
    if (!req->method) {
        free(req->raw_buffer);
        free(req);
        return NULL;
    }
    memcpy(req->method, raw_data, method_len);
    req->method[method_len] = '\0';

    /* Parse path */
    const char *path_start = space + 1;
    const char *path_end = strchr(path_start, ' ');
    if (!path_end) {
        path_end = strchr(path_start, '\r');
    }
    if (!path_end) {
        path_end = path_start + strlen(path_start);
    }

    size_t path_len = (size_t)(path_end - path_start);
    req->path = malloc(path_len + 1);
    if (!req->path) {
        free(req->method);
        free(req->raw_buffer);
        free(req);
        return NULL;
    }
    memcpy(req->path, path_start, path_len);
    req->path[path_len] = '\0';

    /* Allocate headers (simplified) */
    req->headers = strdup("Content-Type: text/html\r\n");
    if (!req->headers) {
        free(req->path);
        free(req->method);
        free(req->raw_buffer);
        free(req);
        return NULL;
    }

    return req;
}

/**
 * @brief Free HTTP request structure
 */
static void free_request(http_request_t *req) {
    if (!req) return;
    free(req->method);
    free(req->path);
    free(req->headers);
    free(req->body);
    free(req->raw_buffer);
    free(req);
}

/**
 * @brief Create HTTP response
 */
static http_response_t *create_response(int status, const char *body) {
    http_response_t *resp = malloc(sizeof(http_response_t));
    if (!resp) {
        return NULL;
    }

    resp->status_code = status;

    /* Create status line and headers */
    const char *status_text = (status == 200) ? "OK" :
                              (status == 404) ? "Not Found" :
                              (status == 500) ? "Internal Server Error" : "Unknown";

    char header_buf[512];
    snprintf(header_buf, sizeof(header_buf),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: text/html\r\n"
             "Connection: close\r\n\r\n",
             status, status_text);

    resp->headers = strdup(header_buf);
    if (!resp->headers) {
        free(resp);
        return NULL;
    }

    if (body) {
        resp->body = strdup(body);
        if (!resp->body) {
            free(resp->headers);
            free(resp);
            return NULL;
        }
        resp->body_length = strlen(body);
    } else {
        resp->body = NULL;
        resp->body_length = 0;
    }

    return resp;
}

/**
 * @brief Free HTTP response structure
 */
static void free_response(http_response_t *resp) {
    if (!resp) return;
    free(resp->headers);
    free(resp->body);
    free(resp);
}

/* ==========================================================================
 * Session Management
 * ========================================================================== */

/**
 * @brief Initialize session store
 */
static void session_store_init(session_store_t *store) {
    pthread_mutex_init(&store->lock, NULL);
    store->next_session_id = 1;
    for (int i = 0; i < MAX_SESSIONS; i++) {
        store->sessions[i].active = false;
        store->sessions[i].username = NULL;
        store->sessions[i].session_data = NULL;
    }
}

/**
 * @brief Destroy session store
 */
static void session_store_destroy(session_store_t *store) {
    pthread_mutex_lock(&store->lock);
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (store->sessions[i].active) {
            free(store->sessions[i].username);
            free(store->sessions[i].session_data);
            store->sessions[i].active = false;
        }
    }
    pthread_mutex_unlock(&store->lock);
    pthread_mutex_destroy(&store->lock);
}

/**
 * @brief Create a new session
 */
static session_t *session_create(session_store_t *store, const char *username) {
    session_t *session = NULL;

    pthread_mutex_lock(&store->lock);

    /* Find free slot */
    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (!store->sessions[i].active) {
            session = &store->sessions[i];
            session->session_id = store->next_session_id++;
            session->username = strdup(username);
            if (!session->username) {
                pthread_mutex_unlock(&store->lock);
                return NULL;
            }

            /* Allocate session data */
            session->session_data = malloc(256);
            if (!session->session_data) {
                free(session->username);
                session->username = NULL;
                pthread_mutex_unlock(&store->lock);
                return NULL;
            }
            snprintf(session->session_data, 256, "user_preferences={theme:dark}");

            session->created_at = time(NULL);
            session->last_access = session->created_at;
            session->active = true;
            break;
        }
    }

    pthread_mutex_unlock(&store->lock);
    return session;
}

/**
 * @brief Clean up expired sessions
 * @note LEAK SCENARIO 2: This function is intentionally incomplete
 *       to demonstrate session timeout leaks
 */
static void session_cleanup_expired(session_store_t *store) {
    time_t now = time(NULL);

    pthread_mutex_lock(&store->lock);

    for (int i = 0; i < MAX_SESSIONS; i++) {
        if (store->sessions[i].active) {
            if ((now - store->sessions[i].last_access) > SESSION_TIMEOUT_SEC) {
                /* LEAK SCENARIO 2: Session data not freed on timeout */
                /* Missing: free(store->sessions[i].session_data); */
                free(store->sessions[i].username);
                store->sessions[i].username = NULL;
                store->sessions[i].session_data = NULL; /* Leaked! */
                store->sessions[i].active = false;
                printf("[SESSION] Session %lu expired\n",
                       (unsigned long)store->sessions[i].session_id);
            }
        }
    }

    pthread_mutex_unlock(&store->lock);
}

/* ==========================================================================
 * Connection Pool Management
 * ========================================================================== */

/**
 * @brief Initialize connection pool
 */
static void connection_pool_init(connection_pool_t *pool) {
    pthread_mutex_init(&pool->lock, NULL);
    pool->active_count = 0;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        pool->connections[i].fd = -1;
        pool->connections[i].in_use = false;
        pool->connections[i].request = NULL;
        pool->connections[i].response = NULL;
        pool->connections[i].session = NULL;
    }
}

/**
 * @brief Destroy connection pool
 */
static void connection_pool_destroy(connection_pool_t *pool) {
    pthread_mutex_lock(&pool->lock);
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (pool->connections[i].in_use) {
            free_request(pool->connections[i].request);
            free_response(pool->connections[i].response);
        }
    }
    pthread_mutex_unlock(&pool->lock);
    pthread_mutex_destroy(&pool->lock);
}

/**
 * @brief Acquire a connection from the pool
 */
static connection_t *connection_acquire(connection_pool_t *pool) {
    connection_t *conn = NULL;

    pthread_mutex_lock(&pool->lock);

    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        if (!pool->connections[i].in_use) {
            conn = &pool->connections[i];
            conn->fd = i;  /* Simulated fd */
            conn->in_use = true;
            conn->request = NULL;
            conn->response = NULL;
            conn->session = NULL;
            conn->connected_at = time(NULL);
            pool->active_count++;
            break;
        }
    }

    pthread_mutex_unlock(&pool->lock);

    if (!conn) {
        fprintf(stderr, "[POOL] Connection pool exhausted!\n");
    }

    return conn;
}

/**
 * @brief Release a connection back to the pool
 */
static void connection_release(connection_pool_t *pool, connection_t *conn) {
    if (!conn) return;

    pthread_mutex_lock(&pool->lock);

    /* LEAK SCENARIO 3: Not always freeing request/response on release */
    /* Intentionally skip cleanup 10% of the time to simulate bugs */
    if ((rand() % 10) != 0) {
        free_request(conn->request);
        free_response(conn->response);
    } else {
        /* LEAK: Request and response buffers leaked */
        printf("[POOL] Connection %d released without cleanup (simulated bug)\n",
               conn->fd);
    }

    conn->request = NULL;
    conn->response = NULL;
    conn->in_use = false;
    conn->fd = -1;
    pool->active_count--;

    pthread_mutex_unlock(&pool->lock);
}

/* ==========================================================================
 * Request Queue
 * ========================================================================== */

/**
 * @brief Initialize request queue
 */
static void request_queue_init(request_queue_t *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->shutdown = false;
    pthread_mutex_init(&queue->lock, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
}

/**
 * @brief Destroy request queue
 */
static void request_queue_destroy(request_queue_t *queue) {
    pthread_mutex_lock(&queue->lock);

    /* Free any remaining items */
    request_queue_item_t *item = queue->head;
    while (item) {
        request_queue_item_t *next = item->next;
        /* Connection cleanup should happen elsewhere */
        free(item);
        item = next;
    }

    pthread_mutex_unlock(&queue->lock);
    pthread_mutex_destroy(&queue->lock);
    pthread_cond_destroy(&queue->not_empty);
}

/**
 * @brief Enqueue a request
 */
static bool request_queue_push(request_queue_t *queue, connection_t *conn) {
    request_queue_item_t *item = malloc(sizeof(request_queue_item_t));
    if (!item) {
        return false;
    }

    item->conn = conn;
    item->next = NULL;

    pthread_mutex_lock(&queue->lock);

    if (queue->tail) {
        queue->tail->next = item;
        queue->tail = item;
    } else {
        queue->head = queue->tail = item;
    }

    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->lock);

    return true;
}

/**
 * @brief Dequeue a request
 */
static connection_t *request_queue_pop(request_queue_t *queue) {
    pthread_mutex_lock(&queue->lock);

    while (!queue->head && !queue->shutdown) {
        pthread_cond_wait(&queue->not_empty, &queue->lock);
    }

    if (queue->shutdown && !queue->head) {
        pthread_mutex_unlock(&queue->lock);
        return NULL;
    }

    request_queue_item_t *item = queue->head;
    queue->head = item->next;
    if (!queue->head) {
        queue->tail = NULL;
    }

    connection_t *conn = item->conn;
    free(item);

    pthread_mutex_unlock(&queue->lock);
    return conn;
}

/**
 * @brief Signal queue shutdown
 */
static void request_queue_shutdown(request_queue_t *queue) {
    pthread_mutex_lock(&queue->lock);
    queue->shutdown = true;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_mutex_unlock(&queue->lock);
}

/* ==========================================================================
 * Request Handler
 * ========================================================================== */

/**
 * @brief Handle a single HTTP request
 */
static void handle_request(connection_t *conn) {
    if (!conn || !conn->request) {
        return;
    }

    http_request_t *req = conn->request;
    http_response_t *resp = NULL;

    printf("[HANDLER] Processing %s %s\n", req->method, req->path);

    /* Route handling */
    if (strcmp(req->path, "/") == 0 || strcmp(req->path, "/index.html") == 0) {
        resp = create_response(200, "<html><body><h1>Welcome to MemRogue Server</h1></body></html>");
    } else if (strcmp(req->path, "/login") == 0) {
        /* Create session on login */
        conn->session = session_create(&g_sessions, "demo_user");
        if (conn->session) {
            resp = create_response(200, "<html><body><h1>Logged In</h1></body></html>");
        } else {
            resp = create_response(500, "<html><body><h1>Session Error</h1></body></html>");
        }
    } else if (strcmp(req->path, "/api/data") == 0) {
        /* Simulate API response with dynamic data */
        char *json_buffer = malloc(1024);
        if (json_buffer) {
            snprintf(json_buffer, 1024,
                     "{\"status\":\"ok\",\"time\":%ld,\"data\":{\"items\":[1,2,3]}}",
                     (long)time(NULL));
            resp = create_response(200, json_buffer);

            /* LEAK SCENARIO 4: Simulating premature disconnect */
            /* Sometimes we "forget" to free the json_buffer */
            if ((rand() % 5) != 0) {
                free(json_buffer);
            } else {
                printf("[HANDLER] Simulated client disconnect - buffer leaked\n");
            }
        } else {
            resp = create_response(500, "Memory allocation failed");
        }
    } else if (strcmp(req->path, "/heavy") == 0) {
        /* Simulate heavy request with large allocation */
        char *large_buffer = malloc(64 * 1024);  /* 64KB */
        if (large_buffer) {
            memset(large_buffer, 'X', 64 * 1024 - 1);
            large_buffer[64 * 1024 - 1] = '\0';
            resp = create_response(200, "Heavy processing completed");
            free(large_buffer);
        } else {
            resp = create_response(500, "Memory allocation failed");
        }
    } else {
        resp = create_response(404, "<html><body><h1>Not Found</h1></body></html>");
    }

    conn->response = resp;

    /* Update statistics */
    pthread_mutex_lock(&g_stats_lock);
    if (resp && resp->status_code == 200) {
        g_requests_processed++;
    } else {
        g_requests_failed++;
    }
    pthread_mutex_unlock(&g_stats_lock);
}

/* ==========================================================================
 * Worker Thread
 * ========================================================================== */

/**
 * @brief Worker thread function
 */
static void *worker_thread(void *arg) {
    int worker_id = *(int *)arg;
    free(arg);

    printf("[WORKER %d] Started\n", worker_id);

    while (!g_shutdown) {
        connection_t *conn = request_queue_pop(&g_request_queue);
        if (!conn) {
            break;  /* Shutdown signal */
        }

        handle_request(conn);

        /* Simulate sending response (would write to socket in real server) */
        if (conn->response) {
            printf("[WORKER %d] Sent response: %d\n",
                   worker_id, conn->response->status_code);
        }

        /* Release connection back to pool */
        connection_release(&g_pool, conn);
    }

    printf("[WORKER %d] Shutting down\n", worker_id);
    return NULL;
}

/* ==========================================================================
 * Simulation Functions
 * ========================================================================== */

/**
 * @brief Simulate an incoming HTTP request
 */
static void simulate_request(const char *method, const char *path) {
    /* Acquire connection */
    connection_t *conn = connection_acquire(&g_pool);
    if (!conn) {
        fprintf(stderr, "[SIM] Failed to acquire connection\n");
        return;
    }

    /* Build raw HTTP request */
    char raw_request[REQUEST_BUFFER_SIZE];
    snprintf(raw_request, sizeof(raw_request),
             "%s %s HTTP/1.1\r\n"
             "Host: localhost\r\n"
             "User-Agent: MemRogue-Sim/1.0\r\n"
             "\r\n",
             method, path);

    /* Parse request */
    conn->request = parse_request(raw_request);
    if (!conn->request) {
        fprintf(stderr, "[SIM] Failed to parse request\n");
        connection_release(&g_pool, conn);
        return;
    }

    /* Queue for processing */
    if (!request_queue_push(&g_request_queue, conn)) {
        fprintf(stderr, "[SIM] Failed to queue request\n");
        free_request(conn->request);
        conn->request = NULL;
        connection_release(&g_pool, conn);
    }
}

/**
 * @brief Run the server simulation
 */
static void run_simulation(void) {
    /* Seed random number generator */
    srand((unsigned int)time(NULL));

    /* Request paths to simulate */
    const char *paths[] = {
        "/",
        "/index.html",
        "/login",
        "/api/data",
        "/heavy",
        "/nonexistent",
        "/api/data",
        "/",
    };
    int num_paths = sizeof(paths) / sizeof(paths[0]);

    printf("\n=== Starting Web Server Simulation ===\n");
    printf("Processing %d requests with %d workers\n\n",
           SIMULATION_REQUESTS, NUM_WORKER_THREADS);

    /* Simulate incoming requests */
    for (int i = 0; i < SIMULATION_REQUESTS; i++) {
        const char *path = paths[i % num_paths];
        simulate_request("GET", path);

        /* Small delay between requests */
        usleep(10000);  /* 10ms */

        /* Periodically clean up expired sessions */
        if (i % 20 == 0) {
            session_cleanup_expired(&g_sessions);
        }
    }

    /* Wait for queue to drain */
    printf("\n[SIM] Waiting for request queue to drain...\n");
    usleep(500000);  /* 500ms */
}

/* ==========================================================================
 * Main Function
 * ========================================================================== */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("MemRogue Web Server Simulation\n");
    printf("===============================\n");
    printf("This example simulates a web server with intentional memory leaks.\n");
    printf("Run with MemRogue to detect:\n");
    printf("  1. Request buffer leaks on error paths\n");
    printf("  2. Session data leaks on timeout\n");
    printf("  3. Connection pool leaks\n");
    printf("  4. Response buffer leaks on disconnect\n\n");

    /* Initialize subsystems */
    connection_pool_init(&g_pool);
    session_store_init(&g_sessions);
    request_queue_init(&g_request_queue);

    /* Create worker threads */
    pthread_t workers[NUM_WORKER_THREADS];
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        int *worker_id = malloc(sizeof(int));
        if (!worker_id) {
            fprintf(stderr, "Failed to allocate worker ID\n");
            continue;
        }
        *worker_id = i;
        if (pthread_create(&workers[i], NULL, worker_thread, worker_id) != 0) {
            fprintf(stderr, "Failed to create worker thread %d\n", i);
            free(worker_id);
        }
    }

    /* Run simulation */
    run_simulation();

    /* Shutdown */
    printf("\n[MAIN] Initiating shutdown...\n");
    g_shutdown = 1;
    request_queue_shutdown(&g_request_queue);

    /* Wait for workers to finish */
    for (int i = 0; i < NUM_WORKER_THREADS; i++) {
        pthread_join(workers[i], NULL);
    }

    /* Print statistics */
    printf("\n=== Simulation Statistics ===\n");
    printf("Requests processed: %d\n", g_requests_processed);
    printf("Requests failed:    %d\n", g_requests_failed);

    /* Cleanup */
    request_queue_destroy(&g_request_queue);
    session_store_destroy(&g_sessions);
    connection_pool_destroy(&g_pool);
    pthread_mutex_destroy(&g_stats_lock);

    printf("\n=== Server Shutdown Complete ===\n");
    printf("Check MemRogue output for detected memory leaks.\n");

    return 0;
}
