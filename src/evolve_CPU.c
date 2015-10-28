#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#define LIBSERG_IMPLEMENTATION
#include <libserg/libserg.h>

#define DJE_IMPLEMENTATION
#include "dummy_jpeg.h"

typedef uint32_t b32;
#ifndef true
#define true 1
#endif  // true
#ifndef false
#define false 0
#endif  // false

#include "gpu.h"



#include "gpu.c"

uint8_t optimal_table[64] = {
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,
};

#define NUM_TABLES_PER_GENERATION 10

typedef struct {
    uint8_t*    table;
    float       fitness;
} PopulationElement;

int pe_comp(const void* va, const void* vb)
{
    PopulationElement* a = (PopulationElement*)va;
    PopulationElement* b = (PopulationElement*)vb;

    float precision = 100000.0f;

    int c = (int)(a->fitness*precision - b->fitness*precision);
    return c;
}


int main()
{
    // Uncomment when doing the actual port to opencl

    /* GPUInfo gpu_info; */
    /* if ( !gpu_init(&gpu_info)) { */
    /*     sgl_log("Could not init GPGPU.\n"); */
    /*     exit(EXIT_FAILURE); */
    /* } */

    size_t memsz = 1L * 1024 * 1024 * 1024;
    Arena root_arena = arena_init(sgl_calloc(memsz, 1), memsz);
    if ( !root_arena.ptr ) {
        sgl_log("Can't allocate memory. Exiting\n");
        exit(EXIT_FAILURE);
    }


    int w, h, ncomp;
    //unsigned char* data = stbi_load("pluto.bmp", &w, &h, &ncomp, 0);
    //unsigned char* data = stbi_load("in.bmp", &w, &h, &ncomp, 0);
    unsigned char* data = stbi_load("in_klay.bmp", &w, &h, &ncomp, 0);

    if ( !data ) {
        puts("Could not load file");
        return EXIT_FAILURE;
    }

    size_t size = (size_t)2 * 1024 * 1024 * 1024;
    void* memory = sgl_calloc(size, 1);
    if ( !memory ) {
        sgl_log("Could not allocate enough memory for the parallel encoder.n");
        return EXIT_FAILURE;
    }

    DJEState base_state = dje_init(&root_arena, w, h, ncomp, data);

    // Optimal state -- The result obtained from using a 1-table. Minimum
    // compression. Maximum quality. The best quality possible for baseline
    // JPEG.
     DJEState optimal_state = base_state;
    dje_encode_main(&optimal_state, optimal_table);

    uint8_t tables[NUM_TABLES_PER_GENERATION][64];

    memcpy(tables[0], optimal_table, 64 * sizeof(uint8_t));
    // Starting from 1 because tables[0] gets filled with ones.
    for (int i = 1; i < NUM_TABLES_PER_GENERATION; ++i) {
        uint8_t* table = tables[i];
        for ( int ti = 0; ti < 64; ++ti ) {
            table[ti] = 1 + (uint8_t)(63.0f * (rand() / (float)RAND_MAX));
        }
    }


    // Fill initial population.
    PopulationElement* population = arena_alloc_array(&root_arena, NUM_TABLES_PER_GENERATION, PopulationElement);
    for (int i = 0; i < NUM_TABLES_PER_GENERATION; ++i) {
        population[i] = (PopulationElement) {
            .table = tables[i],
            .fitness = FLT_MAX,
        };
    }


    Arena iter_arena = arena_push(&root_arena, arena_available_space(&root_arena));

    uint32_t base_bit_count = optimal_state.bit_count / 8;
    float base_mse = (float)optimal_state.mse;

    int num_generations = 20;
    for (int gen_i = 0; gen_i < num_generations; ++gen_i) {
        // Determine fitness.

        for ( int table_i = 0; table_i < NUM_TABLES_PER_GENERATION; ++table_i ) {
            arena_reset(&iter_arena);
            DJEState state = base_state;
            state.arena = &iter_arena;
            dje_encode_main(&state, population[table_i].table);

            uint32_t other_bit_count = state.bit_count / 8;

            float compression_ratio = (float)other_bit_count / (float)base_bit_count;
            // Casting to float. Integers smaller than |2^128| should get rounded.
            // See wiki page on IEEE754
            float error_ratio       = (float)(state.mse) / optimal_state.mse;

            float fitness = error_ratio * compression_ratio;

            population[table_i].fitness = fitness;

            sgl_log("====\n"
                    "QT1 image size: %d\n"
                    "Second image size: %d\n"
                    "Normalized compression ratio: %f\n"
                    "====\n"
                    "QT1 image error %" PRIu64 "\n"
                    "Second image error %" PRIu64 "\n"
                    "Normalized image error ratio %f\n"
                    "Fitness: %f\n\n",
                    base_bit_count, other_bit_count, compression_ratio,
                    optimal_state.mse, state.mse, error_ratio,
                    fitness);
        }

        // Sort by fitness.
        qsort(population, NUM_TABLES_PER_GENERATION, sizeof(PopulationElement), pe_comp);

        // Select two survivors.
        PopulationElement survivors[2] = {
            population[0], population[1],
        };

        int num_mutated = (NUM_TABLES_PER_GENERATION - 2) / 2;
        // Note: NUM_TABLES_PER_GENERATION = 2 + num_mutated + num_crossed
        int num_crossed = NUM_TABLES_PER_GENERATION - 2 - num_mutated;

        int population_index = 2;
        int mutation_wiggle = 4;

        for (int i = 0; i < num_mutated; ++i) {
            uint8_t* table = population[population_index++].table;
            uint8_t* parent = (rand() % 2) ? population[0].table : population[1].table;
            for (int ei = 0; ei < 64; ++ei) {
                table[ei] = parent[ei];
                int dice = (rand() % 64) == 0;
                if ( dice ) {
                    table[ei] += (uint8_t)((rand() % (2*mutation_wiggle)) - mutation_wiggle);
                }
            }
        }

        for (int i = 0; i < num_crossed; ++i) {
            uint8_t* table = population[population_index++].table;

            for ( int ei = 0; ei < 64; ++ei ) {
                int dice = rand() % 2;
                table[ei] = survivors[dice].table[ei];
            }
        }

        // Safety. No invalid tables because JPEG is fragile.
        for (int i = 0; i < population_index; ++i) {
            for (int ei = 0; ei < 64; ++ei) {
                if (population[i].table[ei] <= 0) {
                    population[i].table[ei] = (uint8_t)(1 + (rand() % mutation_wiggle));
                }
            }
        }
    }

    stbi_image_free(data);
    sgl_free(root_arena.ptr);

    return EXIT_SUCCESS;
}
