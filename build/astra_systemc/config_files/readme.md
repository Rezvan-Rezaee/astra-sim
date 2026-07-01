comm_group_config :
    You'd only need it if your Chakra ET tags COMM_COLL_NODEs with different pg_name values that should map to different GPU subsets (e.g., emulating separate data-parallel vs. tensor-parallel groups operating concurrently on different chunks of devices)

intra-dimension-scheduling ----->> set to SCF